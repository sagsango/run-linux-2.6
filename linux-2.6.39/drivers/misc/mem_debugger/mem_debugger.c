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
