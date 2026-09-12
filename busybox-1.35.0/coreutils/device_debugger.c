//config:config DEVICE_DEBUGGER
//config:       bool "device_debugger"
//config:       default y
//applet:APPLET(device_debugger, BB_DIR_USR_BIN, BB_SUID_DROP)
//kbuild:lib-y += device_debugger.o
//usage:#define device_debugger_trivial_usage
//usage:       ""
//usage:#define device_debugger_full_usage "\n\n"
//usage:       "Interactive device debugging ioctl utility"

#include "libbb.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEVICE_NAME "device_debugger"

/*
 * ============================================================
 * IOCTL definitions
 * ============================================================
 *
 * These MUST match device_debugger.c in the kernel.
 */
#define DEVICE_DEBUGGER_MAGIC 'D'
#define DEVICE_DEBUGGER_DUMP_PCI \
        _IO(DEVICE_DEBUGGER_MAGIC, 1)
#define DEVICE_DEBUGGER_DUMP_IRQ \
        _IO(DEVICE_DEBUGGER_MAGIC, 2)
#define DEVICE_DEBUGGER_DUMP_SOFTIRQ \
        _IO(DEVICE_DEBUGGER_MAGIC, 3)

int device_debugger_main(int argc, char **argv)
        MAIN_EXTERNALLY_VISIBLE;
int device_debugger_main(int argc, char **argv)
{
        int fd;
        int choice;
        int ret;
        char buf[32];
        (void)argc;
        (void)argv;
        /*
         * ----------------------------------------------------
         * Open kernel device
         * ----------------------------------------------------
         */

        fd = xopen("/dev/" DEVICE_NAME, O_RDWR);

        printf("\n");
        printf("============================================\n");
        printf("        Device Debugger v0.1\n");
        printf("============================================\n");

        /*
         * ----------------------------------------------------
         * Interactive menu
         * ----------------------------------------------------
         */

        for (;;) {
                printf("\n");
                printf("1. Dump PCI tree\n");
		printf("2. Dump IRQs\n");
		printf("3. Dump softIRQs\n");
                printf("0. Exit\n");
                printf("Select: ");
                fflush(stdout);
                /*
                 * Read user's selection.
                 */
                if (!fgets(buf, sizeof(buf), stdin))
                        break;
                choice = atoi(buf);
                switch (choice) {
                /*
                 * ------------------------------------------------
                 * Dump PCI tree
                 * ------------------------------------------------
                 */
                case 1:
                        //printf("\n");
                        //printf("Dumping PCI tree...\n");
                        //printf("Kernel will print the result using dmesg.\n");
                        //printf("\n");

                        ret = ioctl(fd,
                                    DEVICE_DEBUGGER_DUMP_PCI);

                        if (ret < 0) {
                                printf(
                                    "DEVICE_DEBUGGER_DUMP_PCI failed: %s\n",
                                    strerror(errno));

                        } else {
                                printf(
                                    "PCI tree dump completed.\n");
                        }
                        break;
		case 2:
			ret = ioctl(fd, DEVICE_DEBUGGER_DUMP_IRQ);
			break;
		case 3:
			ret = ioctl(fd, DEVICE_DEBUGGER_DUMP_SOFTIRQ);
			break;
                case 0:
                        close(fd);
                        printf("Exiting device debugger.\n");
                        return 0;

                default:
                        printf("Invalid selection\n");
                        break;
                }
        }


        close(fd);

        return 0;
}
