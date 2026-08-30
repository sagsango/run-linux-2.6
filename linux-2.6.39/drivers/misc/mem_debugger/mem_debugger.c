/*
 * mem_debugger.c
 *
 * Linux 2.6.39
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/ioctl.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/rwlock.h>
#include <linux/mmzone.h>
#include <linux/nodemask.h>
#include "filecache_dump.h"

#define DEVICE_NAME "mem_debugger"

#define MEM_SHOW_FREE_AREAS _IO('M', 1)
#define COMPOUND_PAGE_TEST _IO('M', 2)
#define MEM_DUMP_VMAS       _IO('M', 3)
#define MEM_DUMP_NUMA       _IO('M', 4)
#define MEM_FILECACHE_DUMP  _IO('M', 5)

/* compound_page_test <begin> */
#define TEST_ORDER 2

static struct page *compound_page;
/* compound_page_test <end> */


static dev_t mem_debugger_dev;
static struct cdev mem_debugger_cdev;
static struct class *mem_debugger_class;

static int mem_debugger_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "mem_debugger: open\n");

    return 0;
}

static int mem_debugger_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "mem_debugger: release\n");

    return 0;
}



/*
 * Print the struct page metadata for one page.
 *
 * Linux 2.6.39 struct page contains unions, so some of these
 * fields overlap in memory. We intentionally print the individual
 * interpretations so that the raw metadata can be inspected.
 */
static void dump_page_metadata(struct page *page, unsigned int i)
{
	printk(KERN_INFO "\n");
	printk(KERN_INFO "========================================\n");
	printk(KERN_INFO "page[%u] @ %p\n", i, page);
	printk(KERN_INFO "========================================\n");

	/*
	 * Basic page identity
	 */
	printk(KERN_INFO "  pfn              = %lu\n",
	       page_to_pfn(page));

	printk(KERN_INFO "  physical         = %pa\n",
	       &(phys_addr_t){ page_to_pfn(page) << PAGE_SHIFT });

	/*
	 * struct page members
	 */
	printk(KERN_INFO "\n");
	printk(KERN_INFO "  struct page fields:\n");

	printk(KERN_INFO "    flags          = 0x%lx\n",
	       page->flags);

	printk(KERN_INFO "    _count         = %d\n",
	       atomic_read(&page->_count));

	printk(KERN_INFO "    _mapcount      = %d\n",
	       atomic_read(&page->_mapcount));

	printk(KERN_INFO "    private        = 0x%lx\n",
	       page->private);

	printk(KERN_INFO "    mapping        = %p\n",
	       page->mapping);

	printk(KERN_INFO "    index          = 0x%lx\n",
	       (unsigned long)page->index);

	/*
	 * LRU list.
	 *
	 * struct list_head contains:
	 *
	 *     struct list_head *next;
	 *     struct list_head *prev;
	 */
	printk(KERN_INFO "    lru.next       = %p\n",
	       page->lru.next);

	printk(KERN_INFO "    lru.prev       = %p\n",
	       page->lru.prev);

#ifdef WANT_PAGE_VIRTUAL
	printk(KERN_INFO "    virtual        = %p\n",
	       page->virtual);
#else
	printk(KERN_INFO "    virtual        = <not present>\n");
#endif

	/*
	 * Compound-page metadata.
	 *
	 * first_page is meaningful for compound tail pages.
	 */
	printk(KERN_INFO "    first_page     = %p\n",
	       page->first_page);

	/*
	 * Page state derived from flags.
	 */
	printk(KERN_INFO "\n");
	printk(KERN_INFO "  page state:\n");

	printk(KERN_INFO "    PageHead        = %d\n",
	       PageHead(page));

	printk(KERN_INFO "    PageTail        = %d\n",
	       PageTail(page));

	printk(KERN_INFO "    PageCompound    = %d\n",
	       PageCompound(page));

	printk(KERN_INFO "    PageLRU         = %d\n",
	       PageLRU(page));

	printk(KERN_INFO "    PageReserved    = %d\n",
	       PageReserved(page));

	printk(KERN_INFO "    PagePrivate     = %d\n",
	       PagePrivate(page));

	printk(KERN_INFO "    PageBuddy       = %d\n",
	       PageBuddy(page));

	printk(KERN_INFO "    PageSlab        = %d\n",
	       PageSlab(page));

	printk(KERN_INFO "    PageDirty       = %d\n",
	       PageDirty(page));

	printk(KERN_INFO "    PageWriteback   = %d\n",
	       PageWriteback(page));

	printk(KERN_INFO "    PageLocked      = %d\n",
	       PageLocked(page));

	printk(KERN_INFO "    PageReferenced  = %d\n",
	       PageReferenced(page));

	printk(KERN_INFO "    PageActive      = %d\n",
	       PageActive(page));

	printk(KERN_INFO "    PageUnevictable = %d\n",
	       PageUnevictable(page));

	printk(KERN_INFO "    PageSwapCache   = %d\n",
	       PageSwapCache(page));

	printk(KERN_INFO "    PageSwapBacked  = %d\n",
	       PageSwapBacked(page));

	/*
	 * Compound information.
	 */
	if (PageHead(page)) {
		printk(KERN_INFO "\n");
		printk(KERN_INFO "  compound information:\n");

		printk(KERN_INFO "    compound_order = %u\n",
		       compound_order(page));

		//printk(KERN_INFO "    compound_nr    = %lu\n",
		//       compound_nr(page));
	}

	if (PageTail(page)) {
		printk(KERN_INFO "\n");
		printk(KERN_INFO "  tail information:\n");

		printk(KERN_INFO "    first_page     = %p\n",
		       page->first_page);

		printk(KERN_INFO "    head PFN       = %lu\n",
		       page_to_pfn(page->first_page));
	}
}


static int compound_page_test(void)
{
	unsigned int i;
	unsigned int nr_pages;

	printk(KERN_INFO
	       "compound_test: Linux 2.6.39 compound-page test\n");

	nr_pages = 1U << TEST_ORDER;

	printk(KERN_INFO
	       "compound_test: order=%u nr_pages=%u\n",
	       TEST_ORDER, nr_pages);

	/*
	 * Allocate an order-N allocation.
	 *
	 * alloc_pages() returns the head struct page.
	 */
	compound_page = alloc_pages(__GFP_COMP, TEST_ORDER);

	if (!compound_page) {
		printk(KERN_ERR
		       "compound_test: alloc_pages() failed\n");
		return -ENOMEM;
	}

	printk(KERN_INFO
	       "compound_test: allocated head=%p PFN=%lu\n",
	       compound_page,
	       page_to_pfn(compound_page));

	printk(KERN_INFO
	       "compound_test: compound_order=%u\n",
	       compound_order(compound_page));

	/*
	 * Dump every struct page belonging to the allocation.
	 */
	for (i = 0; i < nr_pages; i++) {
		dump_page_metadata(compound_page + i, i);
		printk("-------   Lets use the library function: dump_page(): --------\n");
		dump_page(compound_page + i);
		printk("--------------------------------------------------------------\n");
	}

	return 0;
}

static void compound_page_test_init(void)
{
	printk(KERN_INFO "compound_page_test_init() ...\n");
}

static void compound_page_test_exit(void)
{
	if (compound_page) {
		printk(KERN_INFO
		       "compound_test: freeing order-%u allocation\n",
		       TEST_ORDER);

		__free_pages(compound_page, TEST_ORDER);

		compound_page = NULL;
	}
}


// Declare the external vmlist variables if they are not exposed in vmalloc.h
extern struct vm_struct *vmlist;
extern rwlock_t vmlist_lock;

static void dump_all_vmas(void)
{
    struct mm_struct *mm = current->mm;
    struct vm_area_struct *vma;
    struct vm_struct *vml;

    printk(KERN_INFO "=== [mem_debugger] Dumping Userspace VMAs for PID %d (%s) ===\n",
           current->pid, current->comm);

    if (!mm) {
        printk(KERN_INFO "No userspace mm context available (kernel thread?).\n");
    } else {
        // Protect mm structure reading
        down_read(&mm->mmap_sem);
        for (vma = mm->mmap; vma; vma = vma->vm_next) {
            printk(KERN_INFO "  User VMA: 0x%08lx - 0x%08lx | Flags: 0x%08lx\n",
                   vma->vm_start, vma->vm_end, vma->vm_flags);
        }
        up_read(&mm->mmap_sem);
    }

    printk(KERN_INFO "=== [mem_debugger] Dumping Kernel vmalloc Areas ===\n");

    // Protect global kernel vmlist reading
    read_lock(&vmlist_lock);
    for (vml = vmlist; vml; vml = vml->next) {
        printk(KERN_INFO "  Kernel VMalloc: 0x%p - 0x%p | Size: %lu bytes | Caller: %pS\n",
               vml->addr, (void *)((unsigned long)vml->addr + vml->size),
               vml->size, vml->caller);
    }
    read_unlock(&vmlist_lock);

    printk(KERN_INFO "=== [mem_debugger] Dump Complete ===\n");
}


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/percpu.h>
#include <linux/cpu.h>

static char * migrate_type_name[MIGRATE_TYPES] = {
	"MIGRATE_UNMOVABLE",
	"MIGRATE_RECLAIMABLE",
	"MIGRATE_MOVABLE",
	"MIGRATE_RESERVE",
	"MIGRATE_ISOLATE"
};
static char * zone_stat_name[NR_VM_ZONE_STAT_ITEMS] = {
        "NR_FREE_PAGES",
        "NR_LRU_BASE",
        "NR_INACTIVE_ANON",  /* must match order of LRU_[IN]ACTIVE */
        "NR_ACTIVE_ANON",         /*  "     "     "   "       "         */
        "NR_INACTIVE_FILE",       /*  "     "     "   "       "         */
        "NR_ACTIVE_FILE",         /*  "     "     "   "       "         */
        "NR_UNEVICTABLE",         /*  "     "     "   "       "         */
        "NR_MLOCK",               /* mlock()ed pages found and moved off LRU */
        "NR_ANON_PAGES",  /* Mapped anonymous pages */
        "NR_FILE_MAPPED", /* pagecache pages mapped into pagetables.
                           only modified from process context */
        "NR_FILE_PAGES",
        "NR_FILE_DIRTY",
        "NR_WRITEBACK",
        "NR_SLAB_RECLAIMABLE",
        "NR_SLAB_UNRECLAIMABLE",
        "NR_PAGETABLE",           /* used for pagetables */
        "NR_KERNEL_STACK",
        /* Second 128 byte cacheline */
        "NR_UNSTABLE_NFS",        /* NFS unstable pages */
        "NR_BOUNCE",
        "NR_VMSCAN_WRITE",
        "NR_WRITEBACK_TEMP",      /* Writeback using temporary buffers */
        "NR_ISOLATED_ANON",       /* Temporary isolated pages from anon lru */
        "NR_ISOLATED_FILE",       /* Temporary isolated pages from file lru */
        "NR_SHMEM",               /* shmem pages (included tmpfs/GEM pages) */
        "NR_DIRTIED",             /* page dirtyings since bootup */
        "NR_WRITTEN",             /* page writings since bootup */
#ifdef CONFIG_NUMA
        "NUMA_HIT",               /* allocated in intended node */
        "NUMA_MISS",              /* allocated in non intended node */
        "NUMA_FOREIGN",           /* was intended here, hit elsewhere */
        "NUMA_INTERLEAVE_HIT",    /* interleaver preferred this zone */
        "NUMA_LOCAL",             /* allocation from local node */
        "NUMA_OTHER",             /* allocation from other node */
#endif
};

static void dump_zone_percpu_pages(struct zone *zone)
{
        int cpu;
        struct per_cpu_pageset *ps;
        struct per_cpu_pages *pcp;
        int i;

        if (!zone) {
                printk(KERN_ERR "zone is NULL\n");
                return;
        }

        //printk(KERN_INFO
        //       "\n==================================================\n");
        printk(KERN_INFO
               "    PER-CPU PAGESET DUMP\n");
        //printk(KERN_INFO
        //       "==================================================\n");

        //printk(KERN_INFO "zone      : %p\n", zone);
        //printk(KERN_INFO "zone name : %s\n", zone->name);
        //printk(KERN_INFO "zone idx  : %d\n", zone_idx(zone));

        /*
         * zone->pageset is a __percpu pointer.
         *
         * Therefore we must obtain the pageset belonging
         * to each CPU separately.
         */
        for_each_online_cpu(cpu) {

                ps = per_cpu_ptr(zone->pageset, cpu);

                if (!ps) {
                        printk(KERN_INFO
                               "      CPU %d: pageset NULL\n", cpu);
                        continue;
                }

                pcp = &ps->pcp;

                printk(KERN_INFO
                       "      _____ CPU %d _____\n",
                       cpu);

                //printk(KERN_INFO
                //       "pageset       : %p\n", ps);

                //printk(KERN_INFO
                //       "pcp           : %p\n", pcp);

                printk(KERN_INFO
                       "      count         : %d pages\n",
                       pcp->count);

                printk(KERN_INFO
                       "      high          : %d pages\n",
                       pcp->high);

                printk(KERN_INFO
                       "      batch         : %d pages\n",
                       pcp->batch);

                //printk(KERN_INFO
                //       "count bytes   : %lu\n",
                //       (unsigned long)pcp->count << PAGE_SHIFT);

                //printk(KERN_INFO
                //       "high bytes    : %lu\n",
                //       (unsigned long)pcp->high << PAGE_SHIFT);

                //printk(KERN_INFO
                //       "batch bytes   : %lu\n",
                //       (unsigned long)pcp->batch << PAGE_SHIFT);

                /*
                 * PCP has one list for each PCP migrate type.
                 */
                printk(KERN_INFO
                       "      PCP MIGRATE LISTS:\n");

                for (i = 0; i < MIGRATE_PCPTYPES; i++) {

                        struct list_head *head;
                        unsigned long nr_pages = 0;
                        struct list_head *pos;

                        head = &pcp->lists[i];

                        /*
                         * Count pages currently on this PCP list.
                         */
                        list_for_each(pos, head)
                                nr_pages++;

                        printk(KERN_INFO
                               "          list[%s] : %lu pages\n",
                               migrate_type_name[i], nr_pages);
                }

#ifdef CONFIG_NUMA
                printk(KERN_INFO
                       "      expire        : %d\n",
                       ps->expire);
#endif

#ifdef CONFIG_SMP
                printk(KERN_INFO
                       "      stat_threshold: %d\n",
                       ps->stat_threshold);

                printk(KERN_INFO
                       "      vm_stat_diff:\n");

                for (i = 0; i < NR_VM_ZONE_STAT_ITEMS; i++) {

                        if (ps->vm_stat_diff[i] != 0) {
                                printk(KERN_INFO
                                       "          stat[%s] = %d\n",
                                       zone_stat_name[i],
                                       ps->vm_stat_diff[i]);
                        }
                }
#endif
        }

        //printk(KERN_INFO
        //       "\n==================================================\n");
}

static void dump_zone(struct zone *zone, int nid, int zid)
{
	printk(KERN_INFO
	       "    ----------------------------------------\n");

	printk(KERN_INFO
	       "    zone[%d]            : %s\n",
	       zid, zone->name);

	printk(KERN_INFO
	       "    zone_start_pfn     : %lu\n",
	       zone->zone_start_pfn);

	printk(KERN_INFO
	       "    spanned_pages      : %lu\n",
	       zone->spanned_pages);

	printk(KERN_INFO
	       "    present_pages      : %lu\n",
	       zone->present_pages);

//	printk(KERN_INFO
//	       "    managed_pages      : %lu\n",
//	       zone->managed_pages);

	printk(KERN_INFO
	       "    free_pages         : %lu\n",
	       zone_page_state(zone, NR_FREE_PAGES));

//	printk(KERN_INFO
//	       "    pages_min          : %lu\n",
//	       zone->pages_min);

//	printk(KERN_INFO
//	       "    pages_low          : %lu\n",
//	       zone->pages_low);

//	printk(KERN_INFO
//	       "    pages_high         : %lu\n",
//	       zone->pages_high);

	printk(KERN_INFO
	       "    zone_pgdat         : %p\n",
	       zone->zone_pgdat);

	printk(KERN_INFO
	       "    zone_pgdat->node_id: %d\n",
	       zone->zone_pgdat->node_id);

	dump_zone_percpu_pages(zone);
        printk(KERN_INFO
               "    ----------------------------------------\n");
}

/* XXX: copied from mm/page_alloc.c */
static char * const zone_names[MAX_NR_ZONES] = {
#ifdef CONFIG_ZONE_DMA
         "DMA",
#endif
#ifdef CONFIG_ZONE_DMA32
         "DMA32",
#endif
         "Normal",
#ifdef CONFIG_HIGHMEM
         "HighMem",
#endif
         "Movable",
};

static char * const zonelist_name[MAX_ZONELISTS] = {
#ifdef CONFIG_NUMA
	"FALLBACK",
#endif
	"NO_FALLBACK"
};

static int dump_zonelist(pg_data_t *pgdat, int nid) {

	int i, j;
	struct zoneref *zr;
	printk(KERN_INFO
	       "zonelist (aka: zone allocation order):\n");

	for (i = 0; i <MAX_ZONELISTS; ++i) {
		printk(KERN_INFO
		       "  nid:%d, zonelist[%s]:", nid, zonelist_name[i]);
		for (j=0; j <= MAX_ZONES_PER_ZONELIST; ++j) {
			zr = &(pgdat->node_zonelists[i]._zonerefs[j]);
			if (!zr->zone) continue;
			printk(KERN_INFO
			       "    [nid:%d, zid:%s] ",
			       zr->zone->node,
			       zone_names[zr->zone_idx]);
		}
	}

	return 0;

}
static void dump_pgdat(pg_data_t *pgdat, int nid)
{
	int zid;

	printk(KERN_INFO "\n");
	printk(KERN_INFO "========================================\n");
	printk(KERN_INFO "NODE %d\n", nid);
	printk(KERN_INFO "========================================\n");

	printk(KERN_INFO
	       "  pgdat              : %p\n",
	       pgdat);

	printk(KERN_INFO
	       "  pgdat->node_id     : %d\n",
	       pgdat->node_id);

	printk(KERN_INFO
	       "  node_start_pfn     : %lu\n",
	       pgdat->node_start_pfn);

	printk(KERN_INFO
	       "  node_present_pages : %lu\n",
	       pgdat->node_present_pages);

	printk(KERN_INFO
	       "  node_spanned_pages : %lu\n",
	       pgdat->node_spanned_pages);

	/*
	 * Traverse all possible zones belonging to this pgdat.
	 */
	for (zid = 0; zid < MAX_NR_ZONES; zid++) {

		struct zone *zone;

		zone = &pgdat->node_zones[zid];

		/*
		 * This zone is not populated.
		 */
		if (zone->present_pages == 0)
			continue;

		dump_zone(zone, nid, zid);
	}

	/* XXX: page allocation order */
	dump_zonelist(pgdat, nid);
}

static void traverse_nodes(void)
{
	int nid;
	pg_data_t *pgdat;

	printk(KERN_INFO "\n");
	printk(KERN_INFO "========================================\n");
	printk(KERN_INFO "       X86 NUMA NODE TRAVERSAL\n");
	printk(KERN_INFO "========================================\n");


	/*
	 * Manually walk all possible NUMA node IDs.
	 */
	for (nid = 0; nid < MAX_NUMNODES; nid++) {

		printk(KERN_INFO "\n");
		printk(KERN_INFO
		       "Checking node %d\n", nid);


		/*
		 * First determine whether this node is online.
		 */
		if (!node_online(nid)) {

			printk(KERN_INFO
			       "  Node %d: OFFLINE\n", nid);

			continue;
		}


		/*
		 * Obtain the pg_data_t belonging to this node.
		 *
		 * x86 NUMA:
		 *
		 *     nid
		 *      |
		 *      v
		 *   NODE_DATA(nid)
		 *      |
		 *      v
		 *   pg_data_t *
		 */
		pgdat = NODE_DATA(nid);


		/*
		 * Sanity check.
		 */
		if (pgdat == NULL) {

			printk(KERN_INFO
			       "  Node %d: ONLINE but pgdat == NULL\n",
			       nid);

			continue;
		}


		printk(KERN_INFO
		       "  Node %d: ONLINE\n", nid);

		printk(KERN_INFO
		       "  NODE_DATA(%d) = %p\n",
		       nid, pgdat);


		/*
		 * Dump the pg_data_t and all its zones.
		 */
		dump_pgdat(pgdat, nid);
	}
}

static long mem_debugger_ioctl(struct file *file,
                               unsigned int cmd,
                               unsigned long arg)
{


    switch (cmd) {

    case MEM_SHOW_FREE_AREAS:
        printk(KERN_INFO
               "mem_debugger: calling show_free_areas()\n");
        show_free_areas();
        break;

    case COMPOUND_PAGE_TEST:
	compound_page_test();
	break;

     case MEM_DUMP_VMAS:
        dump_all_vmas();
        break;

     case MEM_DUMP_NUMA:
	traverse_nodes();
	break;

     case MEM_FILECACHE_DUMP:
	filecache_dump();
	break;

    default:
        return -EINVAL;
    }

    return 0;
}

static const struct file_operations mem_debugger_fops = {
    .owner          = THIS_MODULE,
    .open           = mem_debugger_open,
    .release        = mem_debugger_release,
    .unlocked_ioctl = mem_debugger_ioctl,
};

static int __init mem_debugger_init(void)
{
    int ret;

    printk(KERN_INFO "mem_debugger: init\n");

    ret = alloc_chrdev_region(&mem_debugger_dev,
                              0,
                              1,
                              DEVICE_NAME);
    if (ret)
        return ret;

    cdev_init(&mem_debugger_cdev, &mem_debugger_fops);

    mem_debugger_cdev.owner = THIS_MODULE;

    ret = cdev_add(&mem_debugger_cdev,
                   mem_debugger_dev,
                   1);
    if (ret)
        goto unregister_chrdev;

    mem_debugger_class =
        class_create(THIS_MODULE, DEVICE_NAME);

    if (IS_ERR(mem_debugger_class)) {
        ret = PTR_ERR(mem_debugger_class);
        goto del_cdev;
    }

    device_create(mem_debugger_class,
                  NULL,
                  mem_debugger_dev,
                  NULL,
                  DEVICE_NAME);

    printk(KERN_INFO
           "mem_debugger: registered major=%d minor=%d\n",
           MAJOR(mem_debugger_dev),
           MINOR(mem_debugger_dev));

    compound_page_test_init();

    return 0;

del_cdev:
    cdev_del(&mem_debugger_cdev);

unregister_chrdev:
    unregister_chrdev_region(mem_debugger_dev, 1);

    return ret;
}

static void __exit mem_debugger_exit(void)
{
    printk(KERN_INFO "mem_debugger: exit\n");

    compound_page_test_exit();

    device_destroy(mem_debugger_class,
                   mem_debugger_dev);

    class_destroy(mem_debugger_class);

    cdev_del(&mem_debugger_cdev);

    unregister_chrdev_region(mem_debugger_dev, 1);
}

module_init(mem_debugger_init);
module_exit(mem_debugger_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sagar");
MODULE_DESCRIPTION("Memory debugging module");
