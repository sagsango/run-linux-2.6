/*
 * drivers/misc/iommu_debugger/iommu_debugger.c
 *
 * Linux 2.6.39
 *
 * IOMMU debugging control interface.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

#define IOMMU_DEBUGGER_NAME "iommu_debugger"

static struct dentry *iommu_debugfs_dir;
static struct dentry *iommu_debugfs_control;

static int iommu_debug_enabled;

static ssize_t iommu_debug_control_read(struct file *file,
					char __user *buf,
					size_t count,
					loff_t *ppos)
{
	char tmp[64];
	int len;

	len = snprintf(tmp, sizeof(tmp),
		       "IOMMU debugger: %s\n",
		       iommu_debug_enabled ? "enabled" : "disabled");

	return simple_read_from_buffer(buf, count, ppos, tmp, len);
}

static ssize_t iommu_debug_control_write(struct file *file,
					 const char __user *buf,
					 size_t count,
					 loff_t *ppos)
{
	char tmp[16];

	if (count >= sizeof(tmp))
		return -EINVAL;

	if (copy_from_user(tmp, buf, count))
		return -EFAULT;

	tmp[count] = '\0';

	if (tmp[0] == '1') {
		iommu_debug_enabled = 1;

		pr_info("IOMMU-DBG: debugging ENABLED\n");
	} else if (tmp[0] == '0') {
		iommu_debug_enabled = 0;

		pr_info("IOMMU-DBG: debugging DISABLED\n");
	} else {
		pr_info("IOMMU-DBG: write 1 to enable, 0 to disable\n");
		return -EINVAL;
	}

	return count;
}

static const struct file_operations iommu_debug_control_fops = {
	.owner	= THIS_MODULE,
	.read	= iommu_debug_control_read,
	.write	= iommu_debug_control_write,
};

static int __init iommu_debugger_init(void)
{
	pr_info("IOMMU-DBG: initializing\n");

	iommu_debugfs_dir =
		debugfs_create_dir(IOMMU_DEBUGGER_NAME, NULL);

	if (!iommu_debugfs_dir)
		return -ENOMEM;

	iommu_debugfs_control =
		debugfs_create_file("control",
				    0644,
				    iommu_debugfs_dir,
				    NULL,
				    &iommu_debug_control_fops);

	if (!iommu_debugfs_control) {
		debugfs_remove(iommu_debugfs_dir);
		return -ENOMEM;
	}

	pr_info("IOMMU-DBG: debugfs ready\n");

	return 0;
}

static void __exit iommu_debugger_exit(void)
{
	debugfs_remove_recursive(iommu_debugfs_dir);

	pr_info("IOMMU-DBG: unloaded\n");
}

module_init(iommu_debugger_init);
module_exit(iommu_debugger_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IOMMU Debug Experiment");
MODULE_DESCRIPTION("Linux 2.6.39 Intel IOMMU debugging module");
