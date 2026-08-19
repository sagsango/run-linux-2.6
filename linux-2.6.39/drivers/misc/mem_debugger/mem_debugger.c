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

#define DEVICE_NAME "mem_debugger"

#define MEM_SHOW_FREE_AREAS _IO('M', 1)
#define COMPOUND_PAGE_TEST _IO('M', 2)

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
