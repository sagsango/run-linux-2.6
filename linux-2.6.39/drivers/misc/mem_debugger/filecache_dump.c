/*
 * filecache_dump.c
 *
 * Linux 2.6.39
 *
 * Dumps:
 *
 *   1. Global file/page-cache statistics
 *   2. Per-superblock statistics
 *   3. Per-inode page-cache statistics
 *   4. Per-page state
 *   5. Per-open-file readahead state
 *
 * This is intended as a learning/debugging module for Linux 2.6.39.
 *
 * IMPORTANT:
 *
 * The kernel is changing while we walk these structures. This is
 * diagnostic code, not production code. We deliberately take the
 * appropriate VFS/superblock and mapping locks where practical.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/dcache.h>
#include <linux/radix-tree.h>
#include <linux/page-flags.h>
#include <linux/writeback.h>
#include <linux/swap.h>
#include <linux/pagemap.h>

#include <linux/sched.h>
#include <linux/fdtable.h>
#include <linux/file.h>

#include <linux/list.h>
#include <linux/spinlock.h>
#include "filecache_dump.h"


/*
 * ------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------
 */

static unsigned long pages_to_kb(unsigned long pages)
{
	return pages << (PAGE_SHIFT - 10);
}

#define pages_to_mb ___pages_to_mb__
static unsigned long pages_to_mb(unsigned long pages)
{
	return pages >> (20 - PAGE_SHIFT);
}


/*
 * ------------------------------------------------------------
 * 1. GLOBAL FILE CACHE STATISTICS
 * ------------------------------------------------------------
 */

static void dump_global_file_cache(void)
{
	unsigned long file_pages;
	unsigned long active_file;
	unsigned long inactive_file;
	unsigned long dirty;
	unsigned long writeback;
	unsigned long unevictable;
	unsigned long free_pages;

	file_pages =
		global_page_state(NR_FILE_PAGES);

	active_file =
		global_page_state(NR_ACTIVE_FILE);

	inactive_file =
		global_page_state(NR_INACTIVE_FILE);

	dirty =
		global_page_state(NR_FILE_DIRTY);

	writeback =
		global_page_state(NR_WRITEBACK);

	unevictable =
		global_page_state(NR_UNEVICTABLE);

	free_pages =
		global_page_state(NR_FREE_PAGES);


	printk(KERN_INFO "\n");
	printk(KERN_INFO
	       "====================================================\n");

	printk(KERN_INFO
	       "             GLOBAL FILE CACHE\n");

	printk(KERN_INFO
	       "====================================================\n");

	printk(KERN_INFO
	       "PAGE_SHIFT          : %d\n",
	       PAGE_SHIFT);

	printk(KERN_INFO
	       "PAGE_SIZE           : %lu bytes\n",
	       PAGE_SIZE);

	printk(KERN_INFO
	       "\n");

	printk(KERN_INFO
	       "Total RAM            : %lu pages\n",
	       totalram_pages);

	printk(KERN_INFO
	       "Total RAM            : %lu MB\n",
	       pages_to_mb(totalram_pages));

	printk(KERN_INFO
	       "Free RAM             : %lu pages\n",
	       free_pages);

	printk(KERN_INFO
	       "Free RAM             : %lu MB\n",
	       pages_to_mb(free_pages));

	printk(KERN_INFO
	       "\n");

	printk(KERN_INFO
	       "File cache           : %lu pages\n",
	       file_pages);

	printk(KERN_INFO
	       "File cache           : %lu KB\n",
	       pages_to_kb(file_pages));

	printk(KERN_INFO
	       "File cache           : %lu MB\n",
	       pages_to_mb(file_pages));

	printk(KERN_INFO
	       "\n");

	printk(KERN_INFO
	       "Active file          : %lu pages\n",
	       active_file);

	printk(KERN_INFO
	       "Active file          : %lu KB\n",
	       pages_to_kb(active_file));

	printk(KERN_INFO
	       "Inactive file        : %lu pages\n",
	       inactive_file);

	printk(KERN_INFO
	       "Inactive file        : %lu KB\n",
	       pages_to_kb(inactive_file));

	printk(KERN_INFO
	       "\n");

	printk(KERN_INFO
	       "Dirty file           : %lu pages\n",
	       dirty);

	printk(KERN_INFO
	       "Dirty file           : %lu KB\n",
	       pages_to_kb(dirty));

	printk(KERN_INFO
	       "Writeback            : %lu pages\n",
	       writeback);

	printk(KERN_INFO
	       "Writeback            : %lu KB\n",
	       pages_to_kb(writeback));

	printk(KERN_INFO
	       "\n");

	printk(KERN_INFO
	       "Unevictable          : %lu pages\n",
	       unevictable);

	printk(KERN_INFO
	       "Unevictable          : %lu KB\n",
	       pages_to_kb(unevictable));

	printk(KERN_INFO
	       "====================================================\n");
}


/*
 * ------------------------------------------------------------
 * 2. PAGE STATE
 * ------------------------------------------------------------
 */

static void dump_page_state(struct page *page,
			    unsigned long index)
{

#if 0
	printk(KERN_INFO
	       "          page[%lu] = %p\n",
	       index, page);

	printk(KERN_INFO
	       "              PFN          : %lu\n",
	       page_to_pfn(page));

	printk(KERN_INFO
	       "              index        : %lu\n",
	       index);

	printk(KERN_INFO
	       "              refcount     : %d\n",
	       page_count(page));

	printk(KERN_INFO
	       "              dirty        : %d\n",
	       PageDirty(page));

	printk(KERN_INFO
	       "              writeback    : %d\n",
	       PageWriteback(page));

	printk(KERN_INFO
	       "              uptodate     : %d\n",
	       PageUptodate(page));

	printk(KERN_INFO
	       "              active       : %d\n",
	       PageActive(page));

//	printk(KERN_INFO
//	       "              inactive     : %d\n",
//	       PageInactive(page));

	printk(KERN_INFO
	       "              referenced   : %d\n",
	       PageReferenced(page));

	printk(KERN_INFO
	       "              unevictable  : %d\n",
	       PageUnevictable(page));

	printk(KERN_INFO
	       "              locked       : %d\n",
	       PageLocked(page));

	printk(KERN_INFO
	       "              mapped       : %d\n",
	       page_mapped(page));

	printk(KERN_INFO
	       "\n");
#endif
}


/*
 * ------------------------------------------------------------
 * 3. PER-INODE PAGE CACHE
 * ------------------------------------------------------------
 */

static void dump_inode_cache(struct inode *inode)
{
	struct address_space *mapping;
	struct page *page;
	unsigned long index;
	unsigned long pages_seen = 0;

	unsigned long dirty_pages = 0;
	unsigned long writeback_pages = 0;
	unsigned long active_pages = 0;
	unsigned long inactive_pages = 0;
	unsigned long referenced_pages = 0;

	mapping = inode->i_mapping;

	if (!mapping)
		return;

#if 0
	printk(KERN_INFO "\n");
	printk(KERN_INFO
	       "        +------------------------------------+\n");

	printk(KERN_INFO
	       "        | INODE %lu\n",
	       inode->i_ino);

	printk(KERN_INFO
	       "        +------------------------------------+\n");

	printk(KERN_INFO
	       "        inode address       : %p\n",
	       inode);

	printk(KERN_INFO
	       "        mapping address     : %p\n",
	       mapping);

	printk(KERN_INFO
	       "        inode size          : %lld bytes\n",
	       (long long)inode->i_size);

	printk(KERN_INFO
	       "        mapping->nrpages    : %lu\n",
	       mapping->nrpages);

	printk(KERN_INFO
	       "        cache size          : %lu KB\n",
	       pages_to_kb(mapping->nrpages));

	printk(KERN_INFO
	       "        cache size          : %lu MB\n",
	       pages_to_mb(mapping->nrpages));

	printk(KERN_INFO
	       "        mapping flags       : 0x%lx\n",
	       mapping->flags);

	printk(KERN_INFO
	       "        writeback index     : %lu\n",
	       (unsigned long)mapping->writeback_index);

#endif
	/*
	 * Walk the radix tree.
	 *
	 * We use gang lookup rather than assuming that page indexes
	 * are contiguous.
	 */
	index = 0;

	while (pages_seen < mapping->nrpages) {

		struct page *pages[16];
		unsigned int nr;
		unsigned int i;

		nr = radix_tree_gang_lookup(
			&mapping->page_tree,
			(void **)pages,
			index,
			16);

		if (nr == 0)
			break;

		for (i = 0; i < nr; i++) {

			page = pages[i];

			if (!page)
				continue;

			/*
			 * Account page state.
			 */
			if (PageDirty(page))
				dirty_pages++;

			if (PageWriteback(page))
				writeback_pages++;

			if (PageActive(page))
				active_pages++;

			//if (PageInactive(page))
			//	inactive_pages++;

			if (PageReferenced(page))
				referenced_pages++;

			dump_page_state(
				page,
				page->index);

			/*
			 * Move forward.
			 */
			if (page->index >= index)
				index = page->index + 1;
		}

		/*
		 * Safety against a malformed/non-progressing tree.
		 */
		if (index == 0)
			break;
	}

#if 0

	printk(KERN_INFO
	       "        ------------------------------------\n");

	printk(KERN_INFO
	       "        Pages observed      : %lu\n",
	       pages_seen);

	printk(KERN_INFO
	       "        Dirty pages         : %lu\n",
	       dirty_pages);

	printk(KERN_INFO
	       "        Writeback pages     : %lu\n",
	       writeback_pages);

	printk(KERN_INFO
	       "        Active pages        : %lu\n",
	       active_pages);

	printk(KERN_INFO
	       "        Inactive pages      : %lu\n",
	       inactive_pages);

	printk(KERN_INFO
	       "        Referenced pages    : %lu\n",
	       referenced_pages);

	printk(KERN_INFO
	       "        ------------------------------------\n");
#endif
	printk(KERN_INFO "for inode %d; pages:% %d\n", inode->i_ino,pages_seen); 
}


/*
 * ------------------------------------------------------------
 * 4. PATH
 * ------------------------------------------------------------
 */

static void dump_inode_path(struct inode *inode)
{
	struct dentry *dentry;
	char *buf;
	char *path;

	/*
	 * inode -> alias -> dentry
	 *
	 * We use the first alias available.
	 */
	dentry = NULL;

	if (!list_empty(&inode->i_dentry))
		dentry = list_first_entry(
			&inode->i_dentry,
			struct dentry,
			d_alias);

	if (!dentry)
		return;

	buf = (char *)__get_free_page(GFP_KERNEL);

	if (!buf)
		return;

	path = d_path(
		&(struct path) {
			.mnt = NULL,
			.dentry = dentry
		},
		buf,
		PAGE_SIZE);

	if (!IS_ERR(path)) {

		printk(KERN_INFO
		       "        path                 : %s\n",
		       path);
	}

	free_page((unsigned long)buf);
}


/*
 * ------------------------------------------------------------
 * 5. SUPERBLOCK
 * ------------------------------------------------------------
 */

static void dump_superblock(struct super_block *sb)
{
	printk(KERN_INFO "\n");
	printk(KERN_INFO
	       "====================================================\n");

	printk(KERN_INFO
	       "SUPERBLOCK\n");

	printk(KERN_INFO
	       "====================================================\n");

	printk(KERN_INFO
	       "sb                   : %p\n",
	       sb);

	printk(KERN_INFO
	       "filesystem type      : %s\n",
	       sb->s_type ?
	       sb->s_type->name :
	       "<unknown>");

	printk(KERN_INFO
	       "blocksize            : %lu\n",
	       sb->s_blocksize);

	printk(KERN_INFO
	       "blocksize bits       : %d\n",
	       sb->s_blocksize_bits);

	printk(KERN_INFO
	       "magic                : 0x%lx\n",
	       (unsigned long)sb->s_magic);

	printk(KERN_INFO
	       "flags                : 0x%lx\n",
	       sb->s_flags);

	printk(KERN_INFO
	       "maxbytes             : %lld\n",
	       (long long)sb->s_maxbytes);

	printk(KERN_INFO
	       "====================================================\n");
}


#define s_inode_list_lock s_lock
/*
 * ------------------------------------------------------------
 * 6. INODE WALK FOR A SUPERBLOCK
 * ------------------------------------------------------------
 *
 * Linux keeps an inode list associated with each superblock.
 *
 * We walk sb->s_inodes while holding s_inode_list_lock.
 *
 */

static void dump_superblock_inodes(struct super_block *sb)
{
	struct inode *inode;
	unsigned long count = 0;

	printk(KERN_INFO
	       "\n        INODES FOR SUPERBLOCK\n");

	list_for_each_entry(inode,
			    &sb->s_inodes,
			    i_sb_list) {

#if 0
		printk(KERN_INFO
		       "\n========================================\n");

		printk(KERN_INFO
		       "INODE #%lu\n",
		       count);

		printk(KERN_INFO
		       "========================================\n");

		printk(KERN_INFO
		       "inode address : %p\n",
		       inode);

		printk(KERN_INFO
		       "inode number  : %lu\n",
		       inode->i_ino);

		printk(KERN_INFO
		       "mode          : 0%o\n",
		       inode->i_mode);

		printk(KERN_INFO
		       "uid           : %u\n",
		       inode->i_uid);

		printk(KERN_INFO
		       "gid           : %u\n",
		       inode->i_gid);

		printk(KERN_INFO
		       "nlink         : %u\n",
		       inode->i_nlink);

		printk(KERN_INFO
		       "size          : %lld bytes\n",
		       (long long)inode->i_size);

		printk(KERN_INFO
		       "blocks        : %llu\n",
		       (unsigned long long)inode->i_blocks);

		printk(KERN_INFO
		       "state         : 0x%lx\n",
		       inode->i_state);

		if (inode->i_mapping) {

			printk(KERN_INFO
			       "mapping       : %p\n",
			       inode->i_mapping);

			printk(KERN_INFO
			       "nrpages       : %lu\n",
			       inode->i_mapping->nrpages);

			printk(KERN_INFO
			       "cache KB      : %lu\n",
			       inode->i_mapping->nrpages
			       << (PAGE_SHIFT - 10));
		}
#endif
		count++;
	}

	printk(KERN_INFO
	       "\nTotal inodes walked: %lu\n",
	       count);
}
#if 0
static void dump_superblock_inodes(struct super_block *sb)
{
	struct inode *inode;
	unsigned long count = 0;

	printk(KERN_INFO "\n");
	printk(KERN_INFO
	       "        INODES FOR SUPERBLOCK\n");

//	spin_lock(&sb->s_inode_list_lock);

	list_for_each_entry(inode,
			    &sb->s_inodes,
			    i_sb_list) {

		/*
		 * We cannot safely perform arbitrary operations on
		 * an inode while only holding the inode-list lock.
		 *
		 * Take a reference before dropping the lock.
		 */
		__iget(inode);

		spin_unlock(&sb->s_inode_list_lock);

		printk(KERN_INFO "\n");
		printk(KERN_INFO
		       "================================================\n");

		printk(KERN_INFO
		       "INODE #%lu\n",
		       count++);

		printk(KERN_INFO
		       "================================================\n");

		printk(KERN_INFO
		       "inode address        : %p\n",
		       inode);

		printk(KERN_INFO
		       "inode number         : %lu\n",
		       inode->i_ino);

		printk(KERN_INFO
		       "mode                 : 0%o\n",
		       inode->i_mode);

		printk(KERN_INFO
		       "uid                  : %u\n",
		       inode->i_uid);

		printk(KERN_INFO
		       "gid                  : %u\n",
		       inode->i_gid);

		printk(KERN_INFO
		       "nlink                : %u\n",
		       inode->i_nlink);

		printk(KERN_INFO
		       "size                 : %lld bytes\n",
		       (long long)inode->i_size);

		printk(KERN_INFO
		       "blocks               : %llu\n",
		       (unsigned long long)inode->i_blocks);

		printk(KERN_INFO
		       "state                : 0x%lx\n",
		       inode->i_state);

		dump_inode_path(inode);

		dump_inode_cache(inode);

		iput(inode);

		spin_lock(&sb->s_inode_list_lock);
	}

//	spin_unlock(&sb->s_inode_list_lock);

	printk(KERN_INFO
	       "\nTotal inodes walked: %lu\n",
	       count);
}
#endif

/*
 * ------------------------------------------------------------
 * 7. SUPERBLOCK CALLBACK
 * ------------------------------------------------------------
 */

static void dump_one_super(struct super_block *sb,
			   void *unused)
{
	dump_superblock(sb);

	dump_superblock_inodes(sb);
}


/*
 * ------------------------------------------------------------
 * 8. OPEN FILE / READAHEAD INFORMATION
 * ------------------------------------------------------------
 *
 * This is deliberately separate from inode cache accounting.
 *
 * Multiple struct file objects may reference the same inode.
 *
 * Therefore:
 *
 *     inode->i_mapping->nrpages
 *
 * is cache consumption,
 *
 * while:
 *
 *     file->f_ra
 *
 * is per-open-file readahead state.
 *
 */

static void dump_file_ra(struct file *file)
{
	struct file_ra_state *ra;

	ra = &file->f_ra;

	printk(KERN_INFO
	       "        FILE %p\n",
	       file);
#if 0
	printk(KERN_INFO
	       "          inode            : %lu\n",
	       file->f_dentry &&
	       file->f_dentry->d_inode ?
	       file->f_dentry->d_inode->i_ino :
	       0UL);

	printk(KERN_INFO
	       "          f_pos            : %lld\n",
	       (long long)file->f_pos);

	printk(KERN_INFO
	       "          f_flags          : 0x%x\n",
	       file->f_flags);

	printk(KERN_INFO
	       "          f_mode           : 0x%x\n",
	       file->f_mode);

	printk(KERN_INFO
	       "          readahead start  : %lu\n",
	       (unsigned long)ra->start);

	printk(KERN_INFO
	       "          readahead size   : %u pages\n",
	       ra->size);

	printk(KERN_INFO
	       "          async size       : %u pages\n",
	       ra->async_size);

	printk(KERN_INFO
	       "          max ra pages     : %u pages\n",
	       ra->ra_pages);

	printk(KERN_INFO
	       "          mmap misses      : %u\n",
	       ra->mmap_miss);

	printk(KERN_INFO
	       "          previous pos     : %lld\n",
	       (long long)ra->prev_pos);

	printk(KERN_INFO
	       "\n");
#endif
}


/*
 * ------------------------------------------------------------
 * 9. WALK OPEN FILES
 * ------------------------------------------------------------
 *
 * Walk every process and its fd table.
 *
 * This does NOT represent all page-cache users.
 *
 * It represents currently OPEN struct file objects.
 *
 */

static void dump_open_files(void)
{
	struct task_struct *task;

	printk(KERN_INFO "\n");
	printk(KERN_INFO
	       "====================================================\n");

	printk(KERN_INFO
	       "             OPEN FILE / READAHEAD STATE\n");

	printk(KERN_INFO
	       "====================================================\n");

	for_each_process(task) {

		struct files_struct *files;
		struct fdtable *fdt;
		unsigned int fd;

		files = task->files;

		if (!files)
			continue;

		spin_lock(&files->file_lock);

		fdt = files_fdtable(files);

		for (fd = 0; fd < fdt->max_fds; fd++) {

			struct file *file;

			file = fdt->fd[fd];

			if (!file)
				continue;

			get_file(file);

			spin_unlock(&files->file_lock);

			printk(KERN_INFO
			       "\nPID %d (%s), FD %u\n",
			       task->pid,
			       task->comm,
			       fd);

			dump_file_ra(file);

			fput(file);

			spin_lock(&files->file_lock);

			fdt = files_fdtable(files);
		}

		spin_unlock(&files->file_lock);
	}
}



int filecache_dump(void)
{
	printk(KERN_INFO
	       "\n");
	printk(KERN_INFO
	       "####################################################\n");
	printk(KERN_INFO
	       "# Linux 2.6.39 FILE CACHE DUMP\n");
	printk(KERN_INFO
	       "####################################################\n");


	/*
	 * Global page-cache state.
	 */
	dump_global_file_cache();


	/*
	 * All active superblocks -> all inodes -> page cache.
	 *
	 * iterate_supers() is the VFS mechanism for visiting the
	 * active superblock set.
	 */
	iterate_supers(
		dump_one_super,
		NULL);


	/*
	 * Open files -> per-file readahead state.
	 */
	dump_open_files();


	printk(KERN_INFO
	       "\n");
	printk(KERN_INFO
	       "####################################################\n");
	printk(KERN_INFO
	       "# FILE CACHE DUMP COMPLETE\n");
	printk(KERN_INFO
	       "####################################################\n");

	return 0;
}
