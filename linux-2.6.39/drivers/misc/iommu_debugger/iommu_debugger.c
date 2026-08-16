/*
 * iommu_debugger.c
 *
 * Linux 2.6.39
 *
 * Exercises the real Linux DMA/IOMMU path using PCI device 00:02.0.
 *
 * QEMU device:
 *   00:02.0  Intel Ethernet
 *   vendor = 0x8086
 *   device = 0x10d3
 *
 * debugfs:
 *
 *   /sys/kernel/debug/iommu_debugger/control
 *
 * Write:
 *
 *   echo 1 > control
 *
 * to execute one IOMMU DMA mapping/unmapping test.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/mm.h>

#define DRIVER_NAME "iommu_debugger"

#define TEST_PCI_BUS       0
#define TEST_PCI_SLOT      2
#define TEST_PCI_FUNCTION  0

#define TEST_VENDOR_ID     0x8086
#define TEST_DEVICE_ID     0x10d3

#define TEST_SIZE          PAGE_SIZE

static struct pci_dev *test_pdev;

static struct dentry *debugfs_dir;
static struct dentry *debugfs_control;


/*
 * ------------------------------------------------------------
 * DMA TEST
 * ------------------------------------------------------------
 *
 * This is the important part.
 *
 * We intentionally use the Linux DMA API rather than directly
 * touching Intel IOMMU page tables.
 *
 * On an IOMMU-enabled system:
 *
 *     CPU virtual address
 *             |
 *             v
 *        physical RAM
 *             |
 *             v
 *       IOMMU page table
 *             |
 *             v
 *       DMA/I/O address
 *
 */

static int iommu_run_dma_test(void)
{
	void *cpu_addr;
	dma_addr_t dma_addr;
	unsigned int i;

	pr_info("IOMMU-DBG: ========================================\n");
	pr_info("IOMMU-DBG: starting DMA/IOMMU test\n");

	if (!test_pdev) {
		pr_err("IOMMU-DBG: PCI device is not available\n");
		return -ENODEV;
	}

	pr_info("IOMMU-DBG: PCI device = %s\n",
		pci_name(test_pdev));

	pr_info("IOMMU-DBG: vendor   = 0x%04x\n",
		test_pdev->vendor);

	pr_info("IOMMU-DBG: device   = 0x%04x\n",
		test_pdev->device);

	pr_info("IOMMU-DBG: allocating %d bytes\n",
		TEST_SIZE);


	/*
	 * Allocate ordinary kernel memory.
	 */
	cpu_addr = kmalloc(TEST_SIZE, GFP_KERNEL);

	if (!cpu_addr) {
		pr_err("IOMMU-DBG: kmalloc failed\n");
		return -ENOMEM;
	}

	/*
	 * Put a recognizable pattern into the buffer.
	 */
	memset(cpu_addr, 0x5a, TEST_SIZE);

	pr_info("IOMMU-DBG: CPU virtual address = %p\n",
		cpu_addr);


	/*
	 * --------------------------------------------------------
	 * DMA MAP
	 * --------------------------------------------------------
	 *
	 * This is where the interesting IOMMU work begins.
	 */
	pr_info("IOMMU-DBG: calling dma_map_single()\n");

	dma_addr = dma_map_single(&test_pdev->dev,
				  cpu_addr,
				  TEST_SIZE,
				  DMA_BIDIRECTIONAL);

	if (dma_mapping_error(&test_pdev->dev, dma_addr)) {
		pr_err("IOMMU-DBG: dma_map_single FAILED\n");

		kfree(cpu_addr);
		return -EIO;
	}

	pr_info("IOMMU-DBG: dma_map_single SUCCESS\n");

	pr_info("IOMMU-DBG: DMA address = %pad\n",
		&dma_addr);

	pr_info("IOMMU-DBG: DMA size    = %d\n",
		TEST_SIZE);


	/*
	 * Dump some data so that we know this is a real buffer.
	 */
	pr_info("IOMMU-DBG: buffer contents:\n");

	for (i = 0; i < 32; i++)
		pr_info("IOMMU-DBG: [%02u] = 0x%02x\n",
			i,
			((unsigned char *)cpu_addr)[i]);


	/*
	 * --------------------------------------------------------
	 * SYNC FOR DEVICE
	 * --------------------------------------------------------
	 *
	 * This exercises another part of the DMA API.
	 */
	pr_info("IOMMU-DBG: dma_sync_single_for_device()\n");

	dma_sync_single_for_device(&test_pdev->dev,
				   dma_addr,
				   TEST_SIZE,
				   DMA_BIDIRECTIONAL);


	/*
	 * Normally the actual hardware would now perform DMA using
	 * dma_addr.
	 *
	 * We are not programming the emulated NIC's DMA engine yet.
	 *
	 * The important thing for this first experiment is that the
	 * Linux IOMMU mapping was established.
	 */


	/*
	 * --------------------------------------------------------
	 * SYNC FOR CPU
	 * --------------------------------------------------------
	 */

	pr_info("IOMMU-DBG: dma_sync_single_for_cpu()\n");

	dma_sync_single_for_cpu(&test_pdev->dev,
				dma_addr,
				TEST_SIZE,
				DMA_BIDIRECTIONAL);


	/*
	 * --------------------------------------------------------
	 * UNMAP
	 * --------------------------------------------------------
	 *
	 * This should tear down the DMA mapping.
	 */
	pr_info("IOMMU-DBG: calling dma_unmap_single()\n");

	dma_unmap_single(&test_pdev->dev,
			 dma_addr,
			 TEST_SIZE,
			 DMA_BIDIRECTIONAL);

	pr_info("IOMMU-DBG: dma_unmap_single COMPLETE\n");


	kfree(cpu_addr);

	pr_info("IOMMU-DBG: DMA/IOMMU test complete\n");
	pr_info("IOMMU-DBG: ========================================\n");

	return 0;
}


/*
 * ------------------------------------------------------------
 * DEBUGFS
 * ------------------------------------------------------------
 */

static ssize_t iommu_debugger_read(struct file *file,
				   char __user *buf,
				   size_t count,
				   loff_t *ppos)
{
	char tmp[128];
	int len;

	len = snprintf(tmp,
		       sizeof(tmp),
		       "IOMMU debugger\n"
		       "PCI device: %s\n"
		       "Write 1 to run DMA/IOMMU test\n",
		       test_pdev ? pci_name(test_pdev) : "not found");

	return simple_read_from_buffer(buf,
				       count,
				       ppos,
				       tmp,
				       len);
}


static ssize_t iommu_debugger_write(struct file *file,
				    const char __user *buf,
				    size_t count,
				    loff_t *ppos)
{
	char kbuf[16];
	unsigned long value;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	if (strict_strtoul(kbuf, 0, &value))
		return -EINVAL;

	if (value == 1) {
		pr_info("IOMMU-DBG: test requested\n");

		iommu_run_dma_test();
	} else {
		pr_info("IOMMU-DBG: write 1 to run test\n");
	}

	return count;
}


static const struct file_operations iommu_debugger_fops = {
	.owner = THIS_MODULE,
	.read = iommu_debugger_read,
	.write = iommu_debugger_write,
};


/*
 * ------------------------------------------------------------
 * INIT
 * ------------------------------------------------------------
 */

static int __init iommu_debugger_init(void)
{
	int ret;

	pr_info("IOMMU-DBG: initializing\n");


	/*
	 * Find the actual QEMU PCI device:
	 *
	 * 0000:00:02.0
	 */
	test_pdev = pci_get_domain_bus_and_slot(
			0,
			TEST_PCI_BUS,
			PCI_DEVFN(TEST_PCI_SLOT,
				  TEST_PCI_FUNCTION));

	if (!test_pdev) {
		pr_err("IOMMU-DBG: PCI device 0000:%02x:%02x.%d "
		       "not found\n",
		       TEST_PCI_BUS,
		       TEST_PCI_SLOT,
		       TEST_PCI_FUNCTION);

		return -ENODEV;
	}


	pr_info("IOMMU-DBG: found PCI device %s\n",
		pci_name(test_pdev));

	pr_info("IOMMU-DBG: vendor=0x%04x device=0x%04x\n",
		test_pdev->vendor,
		test_pdev->device);


	/*
	 * Verify that this is the device we expect.
	 */
	if (test_pdev->vendor != TEST_VENDOR_ID ||
	    test_pdev->device != TEST_DEVICE_ID) {

		pr_err("IOMMU-DBG: unexpected PCI device\n");

		pr_err("IOMMU-DBG: expected %04x:%04x\n",
		       TEST_VENDOR_ID,
		       TEST_DEVICE_ID);

		pr_err("IOMMU-DBG: found %04x:%04x\n",
		       test_pdev->vendor,
		       test_pdev->device);

		pci_dev_put(test_pdev);
		test_pdev = NULL;

		return -ENODEV;
	}


	/*
	 * Create:
	 *
	 * /sys/kernel/debug/iommu_debugger
	 */
	debugfs_dir = debugfs_create_dir(DRIVER_NAME, NULL);

	if (!debugfs_dir) {
		pr_err("IOMMU-DBG: failed to create debugfs directory\n");

		pci_dev_put(test_pdev);
		test_pdev = NULL;

		return -ENOMEM;
	}


	/*
	 * Create:
	 *
	 * /sys/kernel/debug/iommu_debugger/control
	 */
	debugfs_control =
		debugfs_create_file("control",
				    0644,
				    debugfs_dir,
				    NULL,
				    &iommu_debugger_fops);

	if (!debugfs_control) {
		pr_err("IOMMU-DBG: failed to create control file\n");

		debugfs_remove(debugfs_dir);

		pci_dev_put(test_pdev);
		test_pdev = NULL;

		return -ENOMEM;
	}


	pr_info("IOMMU-DBG: debugfs ready\n");
	pr_info("IOMMU-DBG: write 1 to control to run test\n");

	return 0;
}


/*
 * ------------------------------------------------------------
 * EXIT
 * ------------------------------------------------------------
 */

static void __exit iommu_debugger_exit(void)
{
	pr_info("IOMMU-DBG: unloading\n");

	debugfs_remove(debugfs_control);
	debugfs_remove(debugfs_dir);

	if (test_pdev) {
		pci_dev_put(test_pdev);
		test_pdev = NULL;
	}

	pr_info("IOMMU-DBG: unloaded\n");
}


module_init(iommu_debugger_init);
module_exit(iommu_debugger_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sagar");
MODULE_DESCRIPTION("Linux 2.6.39 Intel IOMMU DMA debugger");
