/*
 * iommu_debugger.c
 *
 * Linux 2.6.39 Intel IOMMU / DMA API test module
 *
 * Target:
 *     QEMU e1000
 *     PCI 00:02.0
 *     Intel 8086:100e
 *
 * This module intentionally exercises the DMA API:
 *
 *   dma_map_single()
 *   dma_sync_single_for_device()
 *   dma_sync_single_for_cpu()
 *   dma_unmap_single()
 *
 * It also exercises:
 *
 *   DMA_TO_DEVICE
 *   DMA_FROM_DEVICE
 *   DMA_BIDIRECTIONAL
 *
 * multiple buffer sizes and repeated mappings.
 *
 * IMPORTANT:
 * This does NOT itself expose the internal VT-d IOTLB.
 * For that we instrument drivers/pci/intel-iommu.c.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/string.h>

#define DRV_NAME        "iommu_debugger"

#define TEST_VENDOR     PCI_VENDOR_ID_INTEL
#define TEST_DEVICE     0x100e       /* QEMU e1000 */

#define TEST_BUF_SIZE   4096
#define NUM_REPEATS     4

static struct pci_dev *test_pdev;
static struct dentry *debug_dir;
static struct dentry *debug_control;

static int debug_enabled;

/* ------------------------------------------------------------ */
/* Logging                                                       */
/* ------------------------------------------------------------ */

#define IOMMU_DBG(fmt, args...)                         \
	do {                                                \
		if (debug_enabled)                              \
			pr_info("IOMMU-DBG: " fmt, ##args);         \
	} while (0)

#define IOMMU_ERR(fmt, args...)                         \
	pr_err("IOMMU-DBG-ERROR: " fmt, ##args)

/* ------------------------------------------------------------ */
/* Dump PCI information                                         */
/* ------------------------------------------------------------ */

static void dump_pci_info(struct pci_dev *pdev)
{
	u16 vendor;
	u16 device;
	u16 command;
	u16 status;

	pci_read_config_word(pdev, PCI_VENDOR_ID, &vendor);
	pci_read_config_word(pdev, PCI_DEVICE_ID, &device);
	pci_read_config_word(pdev, PCI_COMMAND, &command);
	pci_read_config_word(pdev, PCI_STATUS, &status);

	IOMMU_DBG("PCI DEVICE INFORMATION\n");

	IOMMU_DBG("  bus             = %02x\n",
		  pdev->bus->number);

	IOMMU_DBG("  device          = %02x\n",
		  PCI_SLOT(pdev->devfn));

	IOMMU_DBG("  function        = %x\n",
		  PCI_FUNC(pdev->devfn));

	IOMMU_DBG("  vendor          = %04x\n",
		  vendor);

	IOMMU_DBG("  device          = %04x\n",
		  device);

	IOMMU_DBG("  command         = %04x\n",
		  command);

	IOMMU_DBG("  status          = %04x\n",
		  status);

	IOMMU_DBG("  dma_mask        = %llx\n",
		  (unsigned long long)pdev->dma_mask);

	IOMMU_DBG("  coherent_dma    = %llx\n",
		  (unsigned long long)pdev->dev.coherent_dma_mask);
}

/* ------------------------------------------------------------ */
/* Dump buffer                                                    */
/* ------------------------------------------------------------ */

static void dump_buffer(unsigned char *buf, unsigned int size)
{
	unsigned int i;
	unsigned int count;

	count = size;

	if (count > 64)
		count = 64;

	for (i = 0; i < count; i++) {

		if ((i % 16) == 0)
			IOMMU_DBG("  [%04x] ", i);

		pr_cont("%02x ", buf[i]);

		if ((i % 16) == 15)
			pr_cont("\n");
	}

	if ((count % 16) != 0)
		pr_cont("\n");

	if (size > count)
		IOMMU_DBG("  ... %u bytes omitted\n",
			  size - count);
}

/* ------------------------------------------------------------ */
/* One DMA mapping                                               */
/* ------------------------------------------------------------ */

static int test_dma_mapping(enum dma_data_direction direction,
			    unsigned int size,
			    unsigned char pattern,
			    const char *name)
{
	void *cpu_addr;
	dma_addr_t dma_addr;

	IOMMU_DBG("----------------------------------------\n");
	IOMMU_DBG("DMA TEST: %s\n", name);
	IOMMU_DBG("direction = %d\n", direction);
	IOMMU_DBG("size      = %u\n", size);
	IOMMU_DBG("pattern   = 0x%02x\n", pattern);

	/*
	 * Allocate ordinary kernel memory.
	 */
	cpu_addr = kmalloc(size, GFP_KERNEL);

	if (!cpu_addr) {
		IOMMU_ERR("kmalloc(%u) failed\n", size);
		return -ENOMEM;
	}

	memset(cpu_addr, pattern, size);

	IOMMU_DBG("CPU virtual address = %p\n",
		  cpu_addr);

	IOMMU_DBG("buffer before mapping:\n");
	dump_buffer(cpu_addr, size);

	/*
	 * This is the important transition:
	 *
	 * CPU virtual address
	 *        |
	 *        v
	 * dma_map_single()
	 *        |
	 *        v
	 * DMA/IOMMU address
	 */
	IOMMU_DBG("calling dma_map_single()\n");

	dma_addr = dma_map_single(&test_pdev->dev,
				  cpu_addr,
				  size,
				  direction);

	if (dma_mapping_error(&test_pdev->dev, dma_addr)) {

		IOMMU_ERR("dma_map_single FAILED\n");

		kfree(cpu_addr);

		return -EIO;
	}

	IOMMU_DBG("dma_map_single SUCCESS\n");

	IOMMU_DBG("DMA address = %llx\n",
		  (unsigned long long)dma_addr);

	IOMMU_DBG("DMA size    = %u\n", size);

	/*
	 * Synchronize CPU -> device.
	 */
	IOMMU_DBG("dma_sync_single_for_device()\n");

	dma_sync_single_for_device(&test_pdev->dev,
				   dma_addr,
				   size,
				   direction);

	IOMMU_DBG("device synchronization COMPLETE\n");

	/*
	 * Synchronize device -> CPU.
	 */
	IOMMU_DBG("dma_sync_single_for_cpu()\n");

	dma_sync_single_for_cpu(&test_pdev->dev,
				dma_addr,
				size,
				direction);

	IOMMU_DBG("CPU synchronization COMPLETE\n");

	/*
	 * Unmap.
	 *
	 * This is particularly interesting for the IOMMU because
	 * the Intel IOMMU implementation may have to invalidate
	 * cached translations.
	 */
	IOMMU_DBG("calling dma_unmap_single()\n");

	dma_unmap_single(&test_pdev->dev,
			 dma_addr,
			 size,
			 direction);

	IOMMU_DBG("dma_unmap_single COMPLETE\n");

	kfree(cpu_addr);

	IOMMU_DBG("DMA TEST COMPLETE: %s\n", name);
	IOMMU_DBG("----------------------------------------\n");

	return 0;
}

/* ------------------------------------------------------------ */
/* Repeated mapping                                              */
/* ------------------------------------------------------------ */

static int test_repeated_mapping(void)
{
	void *cpu_addr;
	dma_addr_t dma_addr;
	int i;

	IOMMU_DBG("========================================\n");
	IOMMU_DBG("REPEATED DMA MAP/UNMAP TEST\n");
	IOMMU_DBG("========================================\n");

	cpu_addr = kmalloc(TEST_BUF_SIZE, GFP_KERNEL);

	if (!cpu_addr) {
		IOMMU_ERR("kmalloc failed\n");
		return -ENOMEM;
	}

	memset(cpu_addr, 0xa5, TEST_BUF_SIZE);

	for (i = 0; i < NUM_REPEATS; i++) {

		IOMMU_DBG("REPEAT %d/%d\n",
			  i + 1,
			  NUM_REPEATS);

		IOMMU_DBG("mapping buffer\n");

		dma_addr = dma_map_single(&test_pdev->dev,
					  cpu_addr,
					  TEST_BUF_SIZE,
					  DMA_BIDIRECTIONAL);

		if (dma_mapping_error(&test_pdev->dev,
				      dma_addr)) {

			IOMMU_ERR("mapping failed at iteration %d\n",
				  i);

			kfree(cpu_addr);

			return -EIO;
		}

		IOMMU_DBG("IOVA/DMA address = %llx\n",
			  (unsigned long long)dma_addr);

		IOMMU_DBG("sync for device\n");

		dma_sync_single_for_device(&test_pdev->dev,
					   dma_addr,
					   TEST_BUF_SIZE,
					   DMA_BIDIRECTIONAL);

		IOMMU_DBG("sync for CPU\n");

		dma_sync_single_for_cpu(&test_pdev->dev,
					dma_addr,
					TEST_BUF_SIZE,
					DMA_BIDIRECTIONAL);

		IOMMU_DBG("unmapping\n");

		dma_unmap_single(&test_pdev->dev,
				 dma_addr,
				 TEST_BUF_SIZE,
				 DMA_BIDIRECTIONAL);

		IOMMU_DBG("iteration %d COMPLETE\n",
			  i + 1);
	}

	kfree(cpu_addr);

	IOMMU_DBG("REPEATED TEST COMPLETE\n");

	return 0;
}

/* ------------------------------------------------------------ */
/* Main IOMMU test                                               */
/* ------------------------------------------------------------ */

static int run_iommu_test(void)
{
	int ret;

	if (!test_pdev) {
		IOMMU_ERR("no PCI device\n");
		return -ENODEV;
	}

	IOMMU_DBG("========================================\n");
	IOMMU_DBG("STARTING IOMMU/DMA TEST\n");
	IOMMU_DBG("========================================\n");

	dump_pci_info(test_pdev);

	/*
	 * Test 1
	 */
	ret = test_dma_mapping(DMA_TO_DEVICE,
			       4096,
			       0x5a,
			       "DMA_TO_DEVICE");

	if (ret)
		return ret;

	/*
	 * Test 2
	 */
	ret = test_dma_mapping(DMA_FROM_DEVICE,
			       4096,
			       0xa5,
			       "DMA_FROM_DEVICE");

	if (ret)
		return ret;

	/*
	 * Test 3
	 */
	ret = test_dma_mapping(DMA_BIDIRECTIONAL,
			       4096,
			       0xcc,
			       "DMA_BIDIRECTIONAL");

	if (ret)
		return ret;

	/*
	 * Test 4: different sizes.
	 */
	ret = test_dma_mapping(DMA_BIDIRECTIONAL,
			       64,
			       0x11,
			       "64 BYTE");

	if (ret)
		return ret;

	ret = test_dma_mapping(DMA_BIDIRECTIONAL,
			       4096,
			       0x22,
			       "4 KB");

	if (ret)
		return ret;

	ret = test_dma_mapping(DMA_BIDIRECTIONAL,
			       16384,
			       0x33,
			       "16 KB");

	if (ret)
		return ret;

	/*
	 * Test 5: repeated mapping.
	 */
	ret = test_repeated_mapping();

	if (ret)
		return ret;

	IOMMU_DBG("========================================\n");
	IOMMU_DBG("IOMMU/DMA TEST COMPLETE\n");
	IOMMU_DBG("========================================\n");

	return 0;
}

/* ------------------------------------------------------------ */
/* debugfs control                                               */
/* ------------------------------------------------------------ */

static ssize_t control_read(struct file *file,
			    char __user *buf,
			    size_t count,
			    loff_t *ppos)
{
	char tmp[128];
	int len;

	len = snprintf(tmp,
		       sizeof(tmp),
		       "IOMMU debugger: %s\n",
		       debug_enabled ? "enabled" : "disabled");

	return simple_read_from_buffer(buf,
				       count,
				       ppos,
				       tmp,
				       len);
}

static ssize_t control_write(struct file *file,
			     const char __user *buf,
			     size_t count,
			     loff_t *ppos)
{
	char tmp[32];
	long value;

	if (count >= sizeof(tmp))
		return -EINVAL;

	if (copy_from_user(tmp, buf, count))
		return -EFAULT;

	tmp[count] = '\0';

	if (kstrtol(tmp, 10, &value))
		return -EINVAL;

	if (value == 1) {

		debug_enabled = 1;

		pr_info("IOMMU-DBG: debugging ENABLED\n");

		/*
		 * Run the actual test from the kernel module.
		 */
		run_iommu_test();

	} else if (value == 0) {

		debug_enabled = 0;

		pr_info("IOMMU-DBG: debugging DISABLED\n");

	} else {

		return -EINVAL;
	}

	return count;
}

static const struct file_operations control_fops = {
	.owner = THIS_MODULE,
	.read = control_read,
	.write = control_write,
};

/* ------------------------------------------------------------ */
/* Module initialization                                         */
/* ------------------------------------------------------------ */

static int __init iommu_debugger_init(void)
{
	IOMMU_DBG("initializing\n");

	/*
	 * Find QEMU e1000:
	 *
	 * 00:02.0
	 * 8086:100e
	 */
	test_pdev = pci_get_device(TEST_VENDOR,
				   TEST_DEVICE,
				   NULL);

	if (!test_pdev) {

		pr_err("IOMMU-DBG: PCI device %04x:%04x not found\n",
		       TEST_VENDOR,
		       TEST_DEVICE);

		return -ENODEV;
	}

	pr_info("IOMMU-DBG: found PCI device %04x:%04x\n",
		test_pdev->vendor,
		test_pdev->device);

	pr_info("IOMMU-DBG: PCI address %02x:%02x.%x\n",
		test_pdev->bus->number,
		PCI_SLOT(test_pdev->devfn),
		PCI_FUNC(test_pdev->devfn));

	/*
	 * Don't claim the e1000 device.
	 *
	 * We only use it as the DMA/IOMMU device whose DMA
	 * address space is managed by the IOMMU.
	 */

	debug_dir = debugfs_create_dir("iommu_debugger",
				       NULL);

	if (!debug_dir) {

		pci_dev_put(test_pdev);

		return -ENOMEM;
	}

	debug_control = debugfs_create_file("control",
					    0644,
					    debug_dir,
					    NULL,
					    &control_fops);

	if (!debug_control) {

		debugfs_remove(debug_dir);

		pci_dev_put(test_pdev);

		return -ENOMEM;
	}

	pr_info("IOMMU-DBG: debugfs ready\n");
	pr_info("IOMMU-DBG: write 1 to control to run test\n");

	return 0;
}

/* ------------------------------------------------------------ */
/* Module cleanup                                                */
/* ------------------------------------------------------------ */

static void __exit iommu_debugger_exit(void)
{
	pr_info("IOMMU-DBG: unloading\n");

	if (debug_control)
		debugfs_remove(debug_control);

	if (debug_dir)
		debugfs_remove(debug_dir);

	if (test_pdev)
		pci_dev_put(test_pdev);

	pr_info("IOMMU-DBG: unloaded\n");
}

module_init(iommu_debugger_init);
module_exit(iommu_debugger_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IOMMU Lab");
MODULE_DESCRIPTION("Linux 2.6.39 Intel IOMMU DMA debugger");
