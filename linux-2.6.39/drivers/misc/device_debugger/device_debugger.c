/*
 * device_debugger.c
 *
 * Linux 2.6.39
 * x86
 *
 * Device debugger character device.
 *
 * Creates:
 *
 *      /dev/device_debugger
 *
 * Userspace:
 *
 *      ioctl(fd, DEVICE_DEBUGGER_DUMP_PCI);
 *
 * The ioctl walks:
 *
 *      pci_root_buses
 *              |
 *              +-- struct pci_bus
 *                    |
 *                    +-- bus->devices
 *                    |       |
 *                    |       +-- struct pci_dev
 *                    |
 *                    +-- bus->children
 *                            |
 *                            +-- struct pci_bus
 *                                  |
 *                                  +-- ...
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#include <linux/pci.h>
#include <linux/list.h>
#include <linux/kallsyms.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/interrupt.h>


#define DEVICE_DEBUGGER_NAME        "device_debugger"
#define DEVICE_DEBUGGER_CLASS_NAME  "device_debugger"

#define DEVICE_DEBUGGER_MAGIC       'D'

#define DEVICE_DEBUGGER_DUMP_PCI \
        _IO(DEVICE_DEBUGGER_MAGIC, 1)
#define DEVICE_DEBUGGER_DUMP_IRQ \
	_IO(DEVICE_DEBUGGER_MAGIC, 2)
#define DEVICE_DEBUGGER_DUMP_SOFTIRQ \
	_IO(DEVICE_DEBUGGER_MAGIC, 3)


#define KSYM_NAME_LEN 128


extern int dump_softirqs(void);

static dev_t device_debugger_dev;
static struct cdev device_debugger_cdev;
static struct class *device_debugger_class;

static void dump_symbol(const char * s, unsigned long addr)
{
        char name[KSYM_NAME_LEN];
        unsigned long offset;
        unsigned long size;

        if (!addr) {
                printk(KERN_INFO "%s <none>\n", s);
                return;
        }

	/* XXX: TODO: How it works 
	 *	      also userspace one dlsymbol in glibc*/
        if (lookup_symbol_name(addr, name) < 0) {
                printk(KERN_INFO "%s %p\n", s, (void *)addr);
                return;
        }

        printk(KERN_INFO "%s %p <%s>\n",
               s, (void *)addr, name);
}

static void dump_irq_info(unsigned int irq)
{
        struct irq_desc *desc;
        struct irq_chip *chip;
        struct irqaction *action;

        desc = irq_to_desc(irq);

        if (!desc)
                return;

        printk(KERN_INFO "\n");
        printk(KERN_INFO "============================================================\n");
        printk(KERN_INFO "IRQ %u\n", irq);
        printk(KERN_INFO "============================================================\n");

        /*
         * IRQ CHIP
         */
        chip = desc->irq_data.chip;

        if (chip) {
                printk(KERN_INFO "IRQ CHIP\n");
                printk(KERN_INFO "------------------------------------------------------------\n");

                printk(KERN_INFO "chip              : %s\n",
                       chip->name ? chip->name : "<none>");

                dump_symbol("irq_startup   :",
                            (unsigned long)chip->irq_startup);

                dump_symbol("irq_shutdown  :",
                            (unsigned long)chip->irq_shutdown);

                dump_symbol("irq_enable    :",
                            (unsigned long)chip->irq_enable);

                dump_symbol("irq_disable   :",
                            (unsigned long)chip->irq_disable);

                dump_symbol("irq_ack       :",
                            (unsigned long)chip->irq_ack);

                dump_symbol("irq_mask      :",
                            (unsigned long)chip->irq_mask);

                dump_symbol("irq_mask_ack  :",
                            (unsigned long)chip->irq_mask_ack);

                dump_symbol("irq_unmask    :",
                            (unsigned long)chip->irq_unmask);

                dump_symbol("irq_eoi       :",
                            (unsigned long)chip->irq_eoi);

                dump_symbol("irq_set_affinity:",
                            (unsigned long)chip->irq_set_affinity);

                dump_symbol("irq_retrigger :",
                            (unsigned long)chip->irq_retrigger);

                dump_symbol("irq_set_type  :",
                            (unsigned long)chip->irq_set_type);

                dump_symbol("irq_set_wake  :",
                            (unsigned long)chip->irq_set_wake);
        } else {
                printk(KERN_INFO "IRQ CHIP           : <none>\n");
        }

        /*
         * IRQ ACTION / HANDLERS
         */
        printk(KERN_INFO "\n");
        printk(KERN_INFO "IRQ ACTION / HANDLERS\n");
        printk(KERN_INFO "------------------------------------------------------------\n");

        action = desc->action;

        while (action) {

                printk(KERN_INFO "action            : %p\n", action);

                printk(KERN_INFO "name              : %s\n",
                       action->name ? action->name : "<none>");

                dump_symbol("handler       :",
                            (unsigned long)action->handler);

#ifdef CONFIG_IRQ_THREAD
                dump_symbol("thread_fn     :",
                            (unsigned long)action->thread_fn);
#endif

                printk(KERN_INFO "dev_id            : %p\n",
                       action->dev_id);

                action = action->next;
        }
}

#if 0
static void dump_irq_info(unsigned int irq)
{
        struct irq_desc *desc;
        struct irqaction *action;
        struct irq_chip *chip;

        desc = irq_to_desc(irq);

        if (!desc)
                return;

        if (!desc->action)
                return;

        printk(KERN_INFO "\n");
        printk(KERN_INFO "============================================================\n");
        printk(KERN_INFO "IRQ %u\n", irq);
        printk(KERN_INFO "============================================================\n");

        printk(KERN_INFO "irq_desc       : %p\n", desc);

        /*
         * IRQ DATA
         */
        printk(KERN_INFO "\n");
        printk(KERN_INFO "IRQ DATA\n");
        printk(KERN_INFO "------------------------------------------------------------\n");

        printk(KERN_INFO "irq_data       : %p\n",
               &desc->irq_data);

        printk(KERN_INFO "irq            : %u\n",
               desc->irq_data.irq);

        /*
         * IRQ CHIP
         */
        chip = desc->irq_data.chip;

        printk(KERN_INFO "\n");
        printk(KERN_INFO "IRQ CHIP\n");
        printk(KERN_INFO "------------------------------------------------------------\n");

        if (chip) {
                printk(KERN_INFO "chip           : %p\n", chip);

                printk(KERN_INFO "chip name      : %s\n",
                       chip->name ? chip->name : "<none>");

                dump_symbol("startup       :",
                            (unsigned long)chip->startup);

                dump_symbol("shutdown      :",
                            (unsigned long)chip->shutdown);

                dump_symbol("enable        :",
                            (unsigned long)chip->enable);

                dump_symbol("disable       :",
                            (unsigned long)chip->disable);

                dump_symbol("ack           :",
                            (unsigned long)chip->ack);

                dump_symbol("mask          :",
                            (unsigned long)chip->mask);

                dump_symbol("unmask        :",
                            (unsigned long)chip->unmask);
        } else {
                printk(KERN_INFO "chip           : <none>\n");
        }

        /*
         * IRQ ACTION / TOP HALF
         */
        printk(KERN_INFO "\n");
        printk(KERN_INFO "TOP HALF / IRQ ACTIONS\n");
        printk(KERN_INFO "------------------------------------------------------------\n");

        action = desc->action;

        while (action) {

                printk(KERN_INFO "action         : %p\n", action);

                printk(KERN_INFO "handler        : %p\n",
                       action->handler);

                dump_symbol("handler symbol:",
                            (unsigned long)action->handler);

                printk(KERN_INFO "flags          : 0x%lx\n",
                       action->flags);

                printk(KERN_INFO "name           : %s\n",
                       action->name ?
                       action->name : "<none>");

                printk(KERN_INFO "dev_id         : %p\n",
                       action->dev_id);

                printk(KERN_INFO "next           : %p\n",
                       action->next);

                action = action->next;
        }
}

static void dump_irq_info(unsigned int irq)
{
        struct irq_desc *desc;
        struct irqaction *action;

        printk(KERN_INFO "\n");
        printk(KERN_INFO "============================================================\n");
        printk(KERN_INFO "IRQ INFORMATION\n");
        printk(KERN_INFO "============================================================\n");

        printk(KERN_INFO "IRQ number     : %u\n", irq);

        desc = irq_to_desc(irq);

        if (!desc) {
                printk(KERN_INFO "irq_desc       : <none>\n");
                return;
        }

        printk(KERN_INFO "irq_desc       : %p\n", desc);

        /*
         * IRQ CHIP
         */
        printk(KERN_INFO "\n");
        printk(KERN_INFO "IRQ CHIP\n");
        printk(KERN_INFO "------------------------------------------------------------\n");

        if (desc->chip) {
                printk(KERN_INFO "chip           : %p\n", desc->chip);

                if (desc->chip->name)
                        printk(KERN_INFO "chip name      : %s\n",
                               desc->chip->name);
                else
                        printk(KERN_INFO "chip name      : <none>\n");

                dump_symbol("startup       :",
                            (unsigned long)desc->chip->startup);
                dump_symbol("shutdown      :",
                            (unsigned long)desc->chip->shutdown);
                dump_symbol("enable        :",
                            (unsigned long)desc->chip->enable);
                dump_symbol("disable       :",
                            (unsigned long)desc->chip->disable);
                dump_symbol("ack           :",
                            (unsigned long)desc->chip->ack);
                dump_symbol("mask          :",
                            (unsigned long)desc->chip->mask);
                dump_symbol("unmask        :",
                            (unsigned long)desc->chip->unmask);
        } else {
                printk(KERN_INFO "chip           : <none>\n");
        }

        /*
         * IRQ ACTIONS
         */
        printk(KERN_INFO "\n");
        printk(KERN_INFO "IRQ ACTIONS\n");
        printk(KERN_INFO "------------------------------------------------------------\n");

        action = desc->action;

	while (action) {
		printk(KERN_INFO "action         : %p\n", action);


		printk(KERN_INFO "handler        : %p\n",
		       action->handler);

		dump_symbol("handler symbol:",
			    (unsigned long)action->handler);


		printk(KERN_INFO "flags          : 0x%lx\n",
		       action->flags);

		printk(KERN_INFO "name           : %s\n",
		       action->name ? action->name : "<none>");

		printk(KERN_INFO "dev_id         : %p\n",
		       action->dev_id);

		printk(KERN_INFO "next           : %p\n",
		       action->next);

		action = action->next;
        }
}
#endif

static int dump_one_pci_driver(struct device_driver *drv, void *data)
{
        struct pci_driver *pdrv;

        if (!drv)
                return 0;

        if (drv->bus != &pci_bus_type)
                return 0;

        pdrv = container_of(drv, struct pci_driver, driver);

        printk(KERN_INFO "\n");
        printk(KERN_INFO "============================================================\n");
        printk(KERN_INFO "PCI DRIVER\n");
        printk(KERN_INFO "============================================================\n");

        printk(KERN_INFO "device_driver : %p\n", drv);
        printk(KERN_INFO "pci_driver    : %p\n", pdrv);
        printk(KERN_INFO "name          : %s\n",
               pdrv->name ? pdrv->name : "<none>");

        printk(KERN_INFO "\nCALLBACKS\n");
        printk(KERN_INFO "------------------------------------------------------------\n");


	printk(KERN_INFO "probe         : %p\n", pdrv->probe);
	printk(KERN_INFO "remove        : %p\n", pdrv->remove);
	printk(KERN_INFO "shutdown      : %p\n", pdrv->shutdown);
	printk(KERN_INFO "suspend       : %p\n", pdrv->suspend);
	printk(KERN_INFO "suspend_late  : %p\n", pdrv->suspend_late);
	printk(KERN_INFO "resume_early  : %p\n", pdrv->resume_early);
	printk(KERN_INFO "resume        : %p\n", pdrv->resume);

	dump_symbol("sym(probe):",        (unsigned long)pdrv->probe);
	dump_symbol("sym(remove):",       (unsigned long)pdrv->remove);
	dump_symbol("sym(shutdown):",     (unsigned long)pdrv->shutdown);
	dump_symbol("sym(suspend):",      (unsigned long)pdrv->suspend);
	dump_symbol("sym(suspend_late):", (unsigned long)pdrv->suspend_late);
	dump_symbol("sym(resume_early):", (unsigned long)pdrv->resume_early);
	dump_symbol("sym(resume):",       (unsigned long)pdrv->resume);


	if (pdrv->err_handler) {
		printk(KERN_INFO "err_handler  : %p\n",
		       pdrv->err_handler);

		printk(KERN_INFO "PCI ERROR HANDLERS\n");

		printk(KERN_INFO "error_detected : %p\n",
		       pdrv->err_handler->error_detected);

		printk(KERN_INFO "mmio_enabled   : %p\n",
		       pdrv->err_handler->mmio_enabled);

		printk(KERN_INFO "link_reset     : %p\n",
		       pdrv->err_handler->link_reset);

		printk(KERN_INFO "slot_reset     : %p\n",
		       pdrv->err_handler->slot_reset);

		printk(KERN_INFO "resume         : %p\n",
		       pdrv->err_handler->resume);
	}
	if (pdrv->err_handler) {
		dump_symbol("sym(error_detected):",
			    (unsigned long)pdrv->err_handler->error_detected);

		dump_symbol("sym(mmio_enabled):",
			    (unsigned long)pdrv->err_handler->mmio_enabled);

		dump_symbol("sym(link_reset):",
			    (unsigned long)pdrv->err_handler->link_reset);

		dump_symbol("sym(slot_reset):",
			    (unsigned long)pdrv->err_handler->slot_reset);

		dump_symbol("sym(resume):",
			    (unsigned long)pdrv->err_handler->resume);
	}
        return 0;
}
static int dump_one_driver(struct device_driver *drv, void *data)
{
        printk(KERN_INFO "\n");
        printk(KERN_INFO "============================================================\n");
        printk(KERN_INFO "DRIVER\n");
        printk(KERN_INFO "============================================================\n");

        printk(KERN_INFO "driver address : %p\n", drv);
        printk(KERN_INFO "driver name    : %s\n",
               drv->name ? drv->name : "<none>");

        printk(KERN_INFO "bus            : %s\n",
               drv->bus && drv->bus->name ?
               drv->bus->name : "<none>");

        printk(KERN_INFO "owner          : %p\n",
               drv->owner);

        printk(KERN_INFO "mod_name       : %s\n",
               drv->mod_name ? drv->mod_name : "<none>");

        printk(KERN_INFO "\n");
        printk(KERN_INFO "CALLBACKS\n");
        printk(KERN_INFO "------------------------------------------------------------\n");

        /*
         * Generic driver callbacks
         */

        if (drv->probe)
                printk(KERN_INFO "probe          : %pF\n",
                       drv->probe);
        else
                printk(KERN_INFO "probe          : <none>\n");

        if (drv->remove)
                printk(KERN_INFO "remove         : %pF\n",
                       drv->remove);
        else
                printk(KERN_INFO "remove         : <none>\n");

        if (drv->shutdown)
                printk(KERN_INFO "shutdown       : %pF\n",
                       drv->shutdown);
        else
                printk(KERN_INFO "shutdown       : <none>\n");

        if (drv->suspend)
                printk(KERN_INFO "suspend        : %pF\n",
                       drv->suspend);
        else
                printk(KERN_INFO "suspend        : <none>\n");

        if (drv->resume)
                printk(KERN_INFO "resume         : %pF\n",
                       drv->resume);

        else
                printk(KERN_INFO "resume         : <none>\n");

        /*
         * Power-management operations.
         */
        printk(KERN_INFO "\n");
        printk(KERN_INFO "POWER MANAGEMENT\n");
        printk(KERN_INFO "------------------------------------------------------------\n");

        printk(KERN_INFO "pm              : %p\n",
               drv->pm);

        /*
         * Device-tree matching table.
         */
        printk(KERN_INFO "of_match_table   : %p\n",
               drv->of_match_table);

        /*
         * Attribute groups.
         */
        printk(KERN_INFO "groups           : %p\n",
               drv->groups);

        /*
         * Private driver-model data.
         *
         * We print the address only.
         * We do NOT dereference it here.
         */
        printk(KERN_INFO "driver_private   : %p\n",
               drv->p);

	dump_one_pci_driver(drv, NULL);




        return 0;
}
/*
 * ============================================================
 * Dump one PCI device
 * ============================================================
 */

static void dump_pci_device(struct pci_dev *pdev, int depth)
{
        int i;

        printk(KERN_INFO "\n");

        printk(KERN_INFO
               "%*s+----------------------------------------+\n",
               depth * 4, "");

        printk(KERN_INFO
               "%*s| PCI DEVICE\n",
               depth * 4, "");

        printk(KERN_INFO
               "%*s+----------------------------------------+\n",
               depth * 4, "");

        /*
         * PCI address:
         *
         *      domain:bus:slot.function
         */
        printk(KERN_INFO
               "%*sname        : %s\n",
               depth * 4, "",
               pci_name(pdev));

        printk(KERN_INFO
               "%*spci_dev     : %p\n",
               depth * 4, "",
               pdev);

        /*
         * PCI bus containing this device.
         */
        printk(KERN_INFO
               "%*sbus         : %p\n",
               depth * 4, "",
               pdev->bus);

        /*
         * Device/function number.
         */
        printk(KERN_INFO
               "%*sdevfn       : 0x%02x\n",
               depth * 4, "",
               pdev->devfn);

        printk(KERN_INFO
               "%*sslot        : %d\n",
               depth * 4, "",
               PCI_SLOT(pdev->devfn));

        printk(KERN_INFO
               "%*sfunction    : %d\n",
               depth * 4, "",
               PCI_FUNC(pdev->devfn));

        /*
         * PCI identification.
         */
        printk(KERN_INFO
               "%*svendor      : 0x%04x\n",
               depth * 4, "",
               pdev->vendor);

        printk(KERN_INFO
               "%*sdevice      : 0x%04x\n",
               depth * 4, "",
               pdev->device);

        printk(KERN_INFO
               "%*sclass       : 0x%06x\n",
               depth * 4, "",
               pdev->class);

        printk(KERN_INFO
               "%*srevision    : 0x%02x\n",
               depth * 4, "",
               pdev->revision);

        /*
         * Interrupt.
         */
        printk(KERN_INFO
               "%*sirQ         : %d\n",
               depth * 4, "",
               pdev->irq);

        /*
         * Enable count.
         */
        printk(KERN_INFO
               "%*senable_cnt  : %d\n",
               depth * 4, "",
               pdev->enable_cnt);

        /*
         * ----------------------------------------------------
         * Linux device-model information.
         * ----------------------------------------------------
         */

        printk(KERN_INFO
               "%*sdevice      : %p\n",
               depth * 4, "",
               &pdev->dev);

        printk(KERN_INFO
               "%*sparent[pci-bridge]: %p\n",
               depth * 4, "",
               pdev->dev.parent);

        printk(KERN_INFO
               "%*sdevice name : %s\n",
               depth * 4, "",
               dev_name(&pdev->dev));

        if (pdev->dev.driver) {
                printk(KERN_INFO
                       "%*sdriver      : %s\n",
                       depth * 4, "",
                       pdev->dev.driver->name);
        } else {
                printk(KERN_INFO
                       "%*sdriver      : <none>\n",
                       depth * 4, "");
        }

        /*
         * ----------------------------------------------------
         * PCI bridge information.
         * ----------------------------------------------------
         *
         * For a normal endpoint:
         *
         *      subordinate == NULL
         *
         * For a PCI bridge:
         *
         *      subordinate != NULL
         */

        printk(KERN_INFO
               "%*ssubordinate : %p\n",
               depth * 4, "",
               pdev->subordinate);

        if (pdev->subordinate) {

                printk(KERN_INFO
                       "%*sTYPE        : PCI BRIDGE\n",
                       depth * 4, "");

                printk(KERN_INFO
                       "%*ssubordinate bus number : %d\n",
                       depth * 4, "",
                       pdev->subordinate->number);
        }

        /*
         * ----------------------------------------------------
         * PCI resources / BARs.
         * ----------------------------------------------------
         */

        printk(KERN_INFO
               "%*sBAR RESOURCES:\n",
               depth * 4, "");

        for (i = 0; i < PCI_NUM_RESOURCES; i++) {

                struct resource *res;

                res = &pdev->resource[i];

                /*
                 * Empty resource.
                 */
                if (!res->start && !res->end)
                        continue;

                printk(KERN_INFO
                       "%*s  BAR[%d]\n",
                       depth * 4, "",
                       i);

                printk(KERN_INFO
                       "%*s    start : 0x%lx\n",
                       depth * 4, "",
                       res->start);

                printk(KERN_INFO
                       "%*s    end   : 0x%lx\n",
                       depth * 4, "",
                       res->end);

                printk(KERN_INFO
                       "%*s    flags : 0x%lx\n",
                       depth * 4, "",
                       res->flags);
        }

	if (pdev->dev.driver) {
                dump_one_driver(pdev->dev.driver, NULL);
	}

}


/*
 * ============================================================
 * Dump one PCI bus
 * ============================================================
 *
 * This is the recursive part.
 *
 * For each bus:
 *
 *      1. Walk bus->devices
 *      2. Walk bus->children
 *
 */

static void dump_pci_bus(struct pci_bus *bus, int depth)
{
        struct pci_dev *pdev;
        struct pci_bus *child;

        printk(KERN_INFO "\n");

        printk(KERN_INFO
               "%*s========================================\n",
               depth * 4, "");

        printk(KERN_INFO
               "%*sPCI BUS\n",
               depth * 4, "");

        printk(KERN_INFO
               "%*s========================================\n",
               depth * 4, "");

        printk(KERN_INFO
               "%*sbus address : %p\n",
               depth * 4, "",
               bus);

        printk(KERN_INFO
               "%*sbus number  : %d\n",
               depth * 4, "",
               bus->number);

        printk(KERN_INFO
               "%*sdomain      : %04x\n",
               depth * 4, "",
               pci_domain_nr(bus));

        printk(KERN_INFO
               "%*sparent      : %p\n",
               depth * 4, "",
               bus->parent);

        printk(KERN_INFO
               "%*sself        : %p\n",
               depth * 4, "",
               bus->self);

        /*
         * ----------------------------------------------------
         * Devices directly attached to this bus.
         * ----------------------------------------------------
         */

        printk(KERN_INFO
               "%*s\n",
               depth * 4, "");

        printk(KERN_INFO
               "%*sDEVICES:\n",
               depth * 4, "");

        list_for_each_entry(pdev, &bus->devices, bus_list) {
                dump_pci_device(pdev, depth + 1);
        }

        /*
         * ----------------------------------------------------
         * Child buses.
         * ----------------------------------------------------
         *
         * A child bus normally exists because a PCI bridge
         * leads to another bus.
         */

        printk(KERN_INFO
               "%*s\n",
               depth * 4, "");

        printk(KERN_INFO
               "%*sCHILD BUSES:\n",
               depth * 4, "");

        list_for_each_entry(child, &bus->children, node) {

                dump_pci_bus(child, depth + 1);
        }
}


/*
 * ============================================================
 * Dump all PCI root buses
 * ============================================================
 */

static void dump_all_pci(void)
{
        struct pci_bus *bus;
        int root_count = 0;

        printk(KERN_INFO "\n\n");

        printk(KERN_INFO
               "############################################################\n");

        printk(KERN_INFO
               "#                  DEVICE DEBUGGER                         #\n");

        printk(KERN_INFO
               "#                    PCI TREE                             #\n");

        printk(KERN_INFO
               "############################################################\n");

        /*
         * Start at the PCI root.
         */
        list_for_each_entry(bus, &pci_root_buses, node) {

                printk(KERN_INFO "\n");

                printk(KERN_INFO
                       "******** PCI ROOT BUS %d ********\n",
                       root_count);

                dump_pci_bus(bus, 0);

                root_count++;
        }

        printk(KERN_INFO "\n");

        printk(KERN_INFO
               "PCI ROOT BUS COUNT : %d\n",
               root_count);

        printk(KERN_INFO
               "############################################################\n");

        printk(KERN_INFO
               "#                 END PCI TREE                            #\n");

        printk(KERN_INFO
               "############################################################\n");
}

static void dump_all_irqs(void)
{
        unsigned int irq;

        printk(KERN_INFO "\n");
        printk(KERN_INFO "############################################################\n");
        printk(KERN_INFO "#                    IRQ DEBUGGER                          #\n");
        printk(KERN_INFO "############################################################\n");

        for (irq = 0; irq < NR_IRQS; irq++)
                dump_irq_info(irq);
}
#if 0
static void dump_irq_stats(void)
{
        int cpu;

        printk(KERN_INFO "\n");
        printk(KERN_INFO "============================================================\n");
        printk(KERN_INFO "PER-CPU IRQ STATISTICS\n");
        printk(KERN_INFO "============================================================\n");

        for_each_possible_cpu(cpu) {

                printk(KERN_INFO "\n");
                printk(KERN_INFO "CPU %d\n", cpu);
                printk(KERN_INFO "------------------------------------------------------------\n");

                printk(KERN_INFO "softirq_pending    : %u\n",
                       irq_stat[cpu].__softirq_pending);

                printk(KERN_INFO "nmi_count          : %u\n",
                       irq_stat[cpu].__nmi_count);

                printk(KERN_INFO "irq0_irqs          : %u\n",
                       irq_stat[cpu].irq0_irqs);

#ifdef CONFIG_X86_LOCAL_APIC

                printk(KERN_INFO "apic_timer_irqs    : %u\n",
                       irq_stat[cpu].apic_timer_irqs);

                printk(KERN_INFO "spurious_irqs      : %u\n",
                       irq_stat[cpu].irq_spurious_count);

#endif

                printk(KERN_INFO "platform_ipis      : %u\n",
                       irq_stat[cpu].x86_platform_ipis);

                printk(KERN_INFO "apic_perf_irqs     : %u\n",
                       irq_stat[cpu].apic_perf_irqs);

                printk(KERN_INFO "apic_irq_work_irqs : %u\n",
                       irq_stat[cpu].apic_irq_work_irqs);

#ifdef CONFIG_SMP

                printk(KERN_INFO "reschedule_ipis    : %u\n",
                       irq_stat[cpu].irq_resched_count);

                printk(KERN_INFO "call_function_ipis : %u\n",
                       irq_stat[cpu].irq_call_count);

                printk(KERN_INFO "tlb_ipis           : %u\n",
                       irq_stat[cpu].irq_tlb_count);

#endif

#ifdef CONFIG_X86_THERMAL_VECTOR

                printk(KERN_INFO "thermal_irqs       : %u\n",
                       irq_stat[cpu].irq_thermal_count);

#endif

#ifdef CONFIG_X86_MCE_THRESHOLD

                printk(KERN_INFO "threshold_irqs     : %u\n",
                       irq_stat[cpu].irq_threshold_count);

#endif
        }

        printk(KERN_INFO "\n");
        printk(KERN_INFO "============================================================\n");
        printk(KERN_INFO "END PER-CPU IRQ STATISTICS\n");
        printk(KERN_INFO "============================================================\n");
}
#endif


/*
 * ============================================================
 * ioctl
 * ============================================================
 */

static long device_debugger_ioctl(struct file *file,
                                  unsigned int cmd,
                                  unsigned long arg)
{
#ifndef __ARCH_IRQ_STAT
	/* XXX: TODO: enable me! */
	dump_irq_stats();
#endif
        switch (cmd) {

        case DEVICE_DEBUGGER_DUMP_PCI:

                printk(KERN_INFO
                       "device_debugger: DUMP_PCI\n");

                dump_all_pci();
		break;
	case DEVICE_DEBUGGER_DUMP_IRQ:
		dump_all_irqs();
		break;
	case DEVICE_DEBUGGER_DUMP_SOFTIRQ:
		dump_softirqs();
		break;
        default:

                printk(KERN_ERR
                       "device_debugger: unknown ioctl 0x%x\n",
                       cmd);

                return -EINVAL;
        }
	return 0;
}


/*
 * ============================================================
 * file operations
 * ============================================================
 */

static const struct file_operations device_debugger_fops = {
        .owner          = THIS_MODULE,
        .unlocked_ioctl = device_debugger_ioctl,
};


/*
 * ============================================================
 * Module initialization
 * ============================================================
 */

static int __init device_debugger_init(void)
{
        int ret;

        printk(KERN_INFO
               "device_debugger: initializing\n");

        /*
         * Allocate character device number.
         */
        ret = alloc_chrdev_region(&device_debugger_dev,
                                  0,
                                  1,
                                  DEVICE_DEBUGGER_NAME);

        if (ret) {

                printk(KERN_ERR
                       "device_debugger: alloc_chrdev_region failed\n");

                return ret;
        }

        printk(KERN_INFO
               "device_debugger: major=%d minor=%d\n",
               MAJOR(device_debugger_dev),
               MINOR(device_debugger_dev));


        /*
         * Initialize cdev.
         */
        cdev_init(&device_debugger_cdev,
                  &device_debugger_fops);

        device_debugger_cdev.owner = THIS_MODULE;


        /*
         * Add cdev.
         */
        ret = cdev_add(&device_debugger_cdev,
                       device_debugger_dev,
                       1);

        if (ret) {

                printk(KERN_ERR
                       "device_debugger: cdev_add failed\n");

                unregister_chrdev_region(device_debugger_dev, 1);

                return ret;
        }


        /*
         * Create device class.
         */
        device_debugger_class =
                class_create(THIS_MODULE,
                             DEVICE_DEBUGGER_CLASS_NAME);

        if (IS_ERR(device_debugger_class)) {

                ret = PTR_ERR(device_debugger_class);

                printk(KERN_ERR
                       "device_debugger: class_create failed\n");

                cdev_del(&device_debugger_cdev);

                unregister_chrdev_region(device_debugger_dev, 1);

                return ret;
        }


        /*
         * Create:
         *
         *      /dev/device_debugger
         */
        if (IS_ERR(device_create(device_debugger_class,
                                 NULL,
                                 device_debugger_dev,
                                 NULL,
                                 DEVICE_DEBUGGER_NAME))) {

                printk(KERN_ERR
                       "device_debugger: device_create failed\n");

                class_destroy(device_debugger_class);

                cdev_del(&device_debugger_cdev);

                unregister_chrdev_region(device_debugger_dev, 1);

                return -ENOMEM;
        }


        printk(KERN_INFO
               "device_debugger: /dev/device_debugger created\n");

        return 0;
}


/*
 * ============================================================
 * Module cleanup
 * ============================================================
 */

static void __exit device_debugger_exit(void)
{
        printk(KERN_INFO
               "device_debugger: exiting\n");

        device_destroy(device_debugger_class,
                       device_debugger_dev);

        class_destroy(device_debugger_class);

        cdev_del(&device_debugger_cdev);

        unregister_chrdev_region(device_debugger_dev, 1);

        printk(KERN_INFO
               "device_debugger: removed\n");
}


module_init(device_debugger_init);
module_exit(device_debugger_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sagar");
MODULE_DESCRIPTION("Linux 2.6.39 PCI device tree debugger");

