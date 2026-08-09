/*
 * linux/fs/ext3/ioctl.c
 *
 * Copyright (C) 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 */

#include <linux/fs.h>
#include <linux/jbd.h>
#include <linux/capability.h>
#include <linux/ext3_fs.h>
#include <linux/ext3_jbd.h>
#include <linux/mount.h>
#include <linux/time.h>
#include <linux/compat.h>
#include <asm/uaccess.h>



#include <linux/prio_tree.h>
#include <linux/radix-tree.h>
#include <linux/sched.h>
#include <linux/pagemap.h> // Needed for struct page definitions

#include <linux/mm.h>
#include <asm/pgtable.h>

static void dump_address_space_info(struct file *filp)
{
    struct address_space *mapping = filp->f_mapping;
    struct inode *inode = filp->f_path.dentry->d_inode;
    struct vm_area_struct *vma;
    struct prio_tree_iter iter;

    #define PAGE_BATCH_SIZE 16
    struct page *pages[PAGE_BATCH_SIZE];
    pgoff_t index = 0;
    int found, i;

    printk(KERN_INFO "============= EXT3_IOC_ADDRESS_SPACE START =============\n");
    printk(KERN_INFO "File Name  : %s\n", filp->f_path.dentry->d_name.name);
    printk(KERN_INFO "Inode Num  : %lu\n", (unsigned long)inode->i_ino);
    printk(KERN_INFO "Total Pages: %lu\n", mapping->nrpages);

    // -----------------------------------------------------------------
    // 1. ITERATE PHYSICAL PAGES IN PAGE CACHE
    // -----------------------------------------------------------------
    printk(KERN_INFO "--- Resident Pages in Page Cache ---\n");
    rcu_read_lock();
    do {
        found = radix_tree_gang_lookup(&mapping->page_tree, (void **)pages, index, PAGE_BATCH_SIZE);
        for (i = 0; i < found; i++) {
            struct page *page = pages[i];
            if (!page)
                continue;

            printk(KERN_INFO "  [Page Cache Found] File Page Index: %lu | PFN: %lu | Page Flag Bits: 0x%lx | MapCount: %d\n",
                   (unsigned long)page->index, (unsigned long)page_to_pfn(page), page->flags, atomic_read(&page->_mapcount) + 1);

            if (page->index >= index)
                index = page->index + 1;
        }
    } while (found == PAGE_BATCH_SIZE);
    rcu_read_unlock();

    // -----------------------------------------------------------------
    // 2. ITERATE MEMORY MAPPINGS (VMAs) AND WALK THEIR PAGE TABLES
    // -----------------------------------------------------------------
    printk(KERN_INFO "--- Active Memory Mappings (VMAs) ---\n");
    spin_lock(&mapping->i_mmap_lock);

    vma_prio_tree_foreach(vma, &iter, &mapping->i_mmap, 0, ULONG_MAX) {
        struct mm_struct *mm = vma->vm_mm;
        struct task_struct *task = NULL;
        pid_t owner_pid = 0;
        char owner_comm[TASK_COMM_LEN] = "Unknown";
        unsigned long addr;

        if (mm) {
            rcu_read_lock();
            for_each_process(task) {
                if (task->mm == mm) {
                    owner_pid = task->pid;
                    strlcpy(owner_comm, task->comm, TASK_COMM_LEN);
                    break;
                }
            }
            rcu_read_unlock();
        }

        printk(KERN_INFO "  [VMA Found] Owner Process: %s [%d] | Range: [0x%lx - 0x%lx] | File Page Pgoff: %lu | Flags: 0x%lx\n",
               owner_comm, owner_pid, vma->vm_start, vma->vm_end, vma->vm_pgoff, vma->vm_flags);

        // Skip if there is no memory descriptor associated (e.g., zombie tasks or special kernel states)
        if (!mm)
            continue;

        // Walk the virtual memory address range page by page
        for (addr = vma->vm_start; addr < vma->vm_end; addr += PAGE_SIZE) {
            pgd_t *pgd;
            pud_t *pud;
            pmd_t *pmd;
            pte_t *ptep, pte;
            spinlock_t *ptl;
            unsigned long pfn;

            // 1. Look up Page Global Directory
            pgd = pgd_offset(mm, addr);
            if (pgd_none(*pgd) || pgd_bad(*pgd))
                continue;

            // 2. Look up Page Upper Directory (In x86 32-bit non-PAE, this folds into PGD)
            pud = pud_offset(pgd, addr);
            if (pud_none(*pud) || pud_bad(*pud))
                continue;

            // 3. Look up Page Middle Directory (In x86 32-bit non-PAE, this folds too)
            pmd = pmd_offset(pud, addr);
            if (pmd_none(*pmd) || pmd_bad(*pmd))
                continue;

            // 4. Look up Page Table Entry and lock it to prevent race conditions during read
            ptep = pte_offset_map_lock(mm, pmd, addr, &ptl);
            pte = *ptep;

            // Check if the page table entry is valid and actually present in physical memory
            if (pte_present(pte)) {
                pfn = pte_pfn(pte);
                printk(KERN_INFO "    -> [VMA PFN] Virt Addr: 0x%lx | PFN: %lu\n", addr, pfn);
            }

            // Unlock page table before moving to the next address loop
            pte_unmap_unlock(ptep, ptl);
        }
    }
    spin_unlock(&mapping->i_mmap_lock);

    printk(KERN_INFO "============== EXT3_IOC_ADDRESS_SPACE END ==============\n");
}
#if 0
static void dump_address_space_info(struct file *filp)
{
    struct address_space *mapping = filp->f_mapping;
    struct inode *inode = filp->f_path.dentry->d_inode;
    struct vm_area_struct *vma;
    struct prio_tree_iter iter;

    // Batch lookup configurations for 2.6.39 Radix tree scanning
    #define PAGE_BATCH_SIZE 16
    struct page *pages[PAGE_BATCH_SIZE];
    pgoff_t index = 0;
    int found, i;

    printk(KERN_INFO "============= EXT3_IOC_ADDRESS_SPACE START =============\n");
    printk(KERN_INFO "File Name  : %s\n", filp->f_path.dentry->d_name.name);
    printk(KERN_INFO "Inode Num  : %lu\n", (unsigned long)inode->i_ino);
    printk(KERN_INFO "Total Pages: %lu\n", mapping->nrpages);

    // -----------------------------------------------------------------
    // 1. ITERATE PHYSICAL PAGES SAFELY (2.6.39 Mainline Pattern)
    // -----------------------------------------------------------------
    printk(KERN_INFO "--- Resident Pages in Page Cache ---\n");

    rcu_read_lock(); // Protect the dynamic radix lookup tracking path
    do {
        found = radix_tree_gang_lookup(&mapping->page_tree, (void **)pages, index, PAGE_BATCH_SIZE);
        for (i = 0; i < found; i++) {
            struct page *page = pages[i];

            // Validate pointer and ignore temporary/sentinel tracking states
            if (!page /* || radix_tree_exception(page)*/)
                continue;

            printk(KERN_INFO "  [Page Found] File Page Index: %lu | Page Flag Bits: 0x%lx | MapCount: %d\n",
                   (unsigned long)page->index, page->flags, atomic_read(&page->_mapcount) + 1);

            // Advance lookup index past the processed page index framework
            if (page->index >= index)
                index = page->index + 1;
        }
    } while (found == PAGE_BATCH_SIZE);
    rcu_read_unlock();

    // -----------------------------------------------------------------
    // 2. ITERATE MEMORY MAPPINGS (VMAs) IN THE PRIO TREE
    // -----------------------------------------------------------------
    printk(KERN_INFO "--- Active Memory Mappings (VMAs) ---\n");
    spin_lock(&mapping->i_mmap_lock);

    vma_prio_tree_foreach(vma, &iter, &mapping->i_mmap, 0, ULONG_MAX) {
        struct mm_struct *mm = vma->vm_mm;
        struct task_struct *task = NULL;
        pid_t owner_pid = 0;
        char owner_comm[TASK_COMM_LEN] = "Unknown";

        if (mm) {
            rcu_read_lock();
            for_each_process(task) {
                if (task->mm == mm) {
                    owner_pid = task->pid;
                    strlcpy(owner_comm, task->comm, TASK_COMM_LEN);
                    break;
                }
            }
            rcu_read_unlock();
        }

        printk(KERN_INFO "  [VMA Found] Owner Process: %s [%d] | Range: [0x%lx - 0x%lx] | File Page Pgoff: %lu | Flags: 0x%lx\n",
               owner_comm, owner_pid, vma->vm_start, vma->vm_end, vma->vm_pgoff, vma->vm_flags);
    }
    spin_unlock(&mapping->i_mmap_lock);

    printk(KERN_INFO "============== EXT3_IOC_ADDRESS_SPACE END ==============\n");
}
#endif


long ext3_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct ext3_inode_info *ei = EXT3_I(inode);
	unsigned int flags;
	unsigned short rsv_window_size;

	ext3_debug ("cmd = %u, arg = %lu\n", cmd, arg);

	switch (cmd) {
	case EXT3_IOC_ADDRESS_SPACE:
		// Run our diagnostic dump loop
		dump_address_space_info(filp);
		return 0; // Success return back to user space
	case EXT3_IOC_GETFLAGS:
		ext3_get_inode_flags(ei);
		flags = ei->i_flags & EXT3_FL_USER_VISIBLE;
		return put_user(flags, (int __user *) arg);
	case EXT3_IOC_SETFLAGS: {
		handle_t *handle = NULL;
		int err;
		struct ext3_iloc iloc;
		unsigned int oldflags;
		unsigned int jflag;

		if (!inode_owner_or_capable(inode))
			return -EACCES;

		if (get_user(flags, (int __user *) arg))
			return -EFAULT;

		err = mnt_want_write(filp->f_path.mnt);
		if (err)
			return err;

		flags = ext3_mask_flags(inode->i_mode, flags);

		mutex_lock(&inode->i_mutex);

		/* Is it quota file? Do not allow user to mess with it */
		err = -EPERM;
		if (IS_NOQUOTA(inode))
			goto flags_out;

		oldflags = ei->i_flags;

		/* The JOURNAL_DATA flag is modifiable only by root */
		jflag = flags & EXT3_JOURNAL_DATA_FL;

		/*
		 * The IMMUTABLE and APPEND_ONLY flags can only be changed by
		 * the relevant capability.
		 *
		 * This test looks nicer. Thanks to Pauline Middelink
		 */
		if ((flags ^ oldflags) & (EXT3_APPEND_FL | EXT3_IMMUTABLE_FL)) {
			if (!capable(CAP_LINUX_IMMUTABLE))
				goto flags_out;
		}

		/*
		 * The JOURNAL_DATA flag can only be changed by
		 * the relevant capability.
		 */
		if ((jflag ^ oldflags) & (EXT3_JOURNAL_DATA_FL)) {
			if (!capable(CAP_SYS_RESOURCE))
				goto flags_out;
		}

		handle = ext3_journal_start(inode, 1);
		if (IS_ERR(handle)) {
			err = PTR_ERR(handle);
			goto flags_out;
		}
		if (IS_SYNC(inode))
			handle->h_sync = 1;
		err = ext3_reserve_inode_write(handle, inode, &iloc);
		if (err)
			goto flags_err;

		flags = flags & EXT3_FL_USER_MODIFIABLE;
		flags |= oldflags & ~EXT3_FL_USER_MODIFIABLE;
		ei->i_flags = flags;

		ext3_set_inode_flags(inode);
		inode->i_ctime = CURRENT_TIME_SEC;

		err = ext3_mark_iloc_dirty(handle, inode, &iloc);
flags_err:
		ext3_journal_stop(handle);
		if (err)
			goto flags_out;

		if ((jflag ^ oldflags) & (EXT3_JOURNAL_DATA_FL))
			err = ext3_change_inode_journal_flag(inode, jflag);
flags_out:
		mutex_unlock(&inode->i_mutex);
		mnt_drop_write(filp->f_path.mnt);
		return err;
	}
	case EXT3_IOC_GETVERSION:
	case EXT3_IOC_GETVERSION_OLD:
		return put_user(inode->i_generation, (int __user *) arg);
	case EXT3_IOC_SETVERSION:
	case EXT3_IOC_SETVERSION_OLD: {
		handle_t *handle;
		struct ext3_iloc iloc;
		__u32 generation;
		int err;

		if (!inode_owner_or_capable(inode))
			return -EPERM;

		err = mnt_want_write(filp->f_path.mnt);
		if (err)
			return err;
		if (get_user(generation, (int __user *) arg)) {
			err = -EFAULT;
			goto setversion_out;
		}

		handle = ext3_journal_start(inode, 1);
		if (IS_ERR(handle)) {
			err = PTR_ERR(handle);
			goto setversion_out;
		}
		err = ext3_reserve_inode_write(handle, inode, &iloc);
		if (err == 0) {
			inode->i_ctime = CURRENT_TIME_SEC;
			inode->i_generation = generation;
			err = ext3_mark_iloc_dirty(handle, inode, &iloc);
		}
		ext3_journal_stop(handle);
setversion_out:
		mnt_drop_write(filp->f_path.mnt);
		return err;
	}
#ifdef CONFIG_JBD_DEBUG
	case EXT3_IOC_WAIT_FOR_READONLY:
		/*
		 * This is racy - by the time we're woken up and running,
		 * the superblock could be released.  And the module could
		 * have been unloaded.  So sue me.
		 *
		 * Returns 1 if it slept, else zero.
		 */
		{
			struct super_block *sb = inode->i_sb;
			DECLARE_WAITQUEUE(wait, current);
			int ret = 0;

			set_current_state(TASK_INTERRUPTIBLE);
			add_wait_queue(&EXT3_SB(sb)->ro_wait_queue, &wait);
			if (timer_pending(&EXT3_SB(sb)->turn_ro_timer)) {
				schedule();
				ret = 1;
			}
			remove_wait_queue(&EXT3_SB(sb)->ro_wait_queue, &wait);
			return ret;
		}
#endif
	case EXT3_IOC_GETRSVSZ:
		if (test_opt(inode->i_sb, RESERVATION)
			&& S_ISREG(inode->i_mode)
			&& ei->i_block_alloc_info) {
			rsv_window_size = ei->i_block_alloc_info->rsv_window_node.rsv_goal_size;
			return put_user(rsv_window_size, (int __user *)arg);
		}
		return -ENOTTY;
	case EXT3_IOC_SETRSVSZ: {
		int err;

		if (!test_opt(inode->i_sb, RESERVATION) ||!S_ISREG(inode->i_mode))
			return -ENOTTY;

		err = mnt_want_write(filp->f_path.mnt);
		if (err)
			return err;

		if (!inode_owner_or_capable(inode)) {
			err = -EACCES;
			goto setrsvsz_out;
		}

		if (get_user(rsv_window_size, (int __user *)arg)) {
			err = -EFAULT;
			goto setrsvsz_out;
		}

		if (rsv_window_size > EXT3_MAX_RESERVE_BLOCKS)
			rsv_window_size = EXT3_MAX_RESERVE_BLOCKS;

		/*
		 * need to allocate reservation structure for this inode
		 * before set the window size
		 */
		mutex_lock(&ei->truncate_mutex);
		if (!ei->i_block_alloc_info)
			ext3_init_block_alloc_info(inode);

		if (ei->i_block_alloc_info){
			struct ext3_reserve_window_node *rsv = &ei->i_block_alloc_info->rsv_window_node;
			rsv->rsv_goal_size = rsv_window_size;
		}
		mutex_unlock(&ei->truncate_mutex);
setrsvsz_out:
		mnt_drop_write(filp->f_path.mnt);
		return err;
	}
	case EXT3_IOC_GROUP_EXTEND: {
		ext3_fsblk_t n_blocks_count;
		struct super_block *sb = inode->i_sb;
		int err, err2;

		if (!capable(CAP_SYS_RESOURCE))
			return -EPERM;

		err = mnt_want_write(filp->f_path.mnt);
		if (err)
			return err;

		if (get_user(n_blocks_count, (__u32 __user *)arg)) {
			err = -EFAULT;
			goto group_extend_out;
		}
		err = ext3_group_extend(sb, EXT3_SB(sb)->s_es, n_blocks_count);
		journal_lock_updates(EXT3_SB(sb)->s_journal);
		err2 = journal_flush(EXT3_SB(sb)->s_journal);
		journal_unlock_updates(EXT3_SB(sb)->s_journal);
		if (err == 0)
			err = err2;
group_extend_out:
		mnt_drop_write(filp->f_path.mnt);
		return err;
	}
	case EXT3_IOC_GROUP_ADD: {
		struct ext3_new_group_data input;
		struct super_block *sb = inode->i_sb;
		int err, err2;

		if (!capable(CAP_SYS_RESOURCE))
			return -EPERM;

		err = mnt_want_write(filp->f_path.mnt);
		if (err)
			return err;

		if (copy_from_user(&input, (struct ext3_new_group_input __user *)arg,
				sizeof(input))) {
			err = -EFAULT;
			goto group_add_out;
		}

		err = ext3_group_add(sb, &input);
		journal_lock_updates(EXT3_SB(sb)->s_journal);
		err2 = journal_flush(EXT3_SB(sb)->s_journal);
		journal_unlock_updates(EXT3_SB(sb)->s_journal);
		if (err == 0)
			err = err2;
group_add_out:
		mnt_drop_write(filp->f_path.mnt);
		return err;
	}
	case FITRIM: {

		struct super_block *sb = inode->i_sb;
		struct fstrim_range range;
		int ret = 0;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (copy_from_user(&range, (struct fstrim_range *)arg,
				   sizeof(range)))
			return -EFAULT;

		ret = ext3_trim_fs(sb, &range);
		if (ret < 0)
			return ret;

		if (copy_to_user((struct fstrim_range *)arg, &range,
				 sizeof(range)))
			return -EFAULT;

		return 0;
	}

	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_COMPAT
long ext3_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	/* These are just misnamed, they actually get/put from/to user an int */
	switch (cmd) {
	case EXT3_IOC32_GETFLAGS:
		cmd = EXT3_IOC_GETFLAGS;
		break;
	case EXT3_IOC32_SETFLAGS:
		cmd = EXT3_IOC_SETFLAGS;
		break;
	case EXT3_IOC32_GETVERSION:
		cmd = EXT3_IOC_GETVERSION;
		break;
	case EXT3_IOC32_SETVERSION:
		cmd = EXT3_IOC_SETVERSION;
		break;
	case EXT3_IOC32_GROUP_EXTEND:
		cmd = EXT3_IOC_GROUP_EXTEND;
		break;
	case EXT3_IOC32_GETVERSION_OLD:
		cmd = EXT3_IOC_GETVERSION_OLD;
		break;
	case EXT3_IOC32_SETVERSION_OLD:
		cmd = EXT3_IOC_SETVERSION_OLD;
		break;
#ifdef CONFIG_JBD_DEBUG
	case EXT3_IOC32_WAIT_FOR_READONLY:
		cmd = EXT3_IOC_WAIT_FOR_READONLY;
		break;
#endif
	case EXT3_IOC32_GETRSVSZ:
		cmd = EXT3_IOC_GETRSVSZ;
		break;
	case EXT3_IOC32_SETRSVSZ:
		cmd = EXT3_IOC_SETRSVSZ;
		break;
	case EXT3_IOC_GROUP_ADD:
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return ext3_ioctl(file, cmd, (unsigned long) compat_ptr(arg));
}
#endif
