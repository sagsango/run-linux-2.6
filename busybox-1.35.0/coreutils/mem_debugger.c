//config:config MEM_DEBUGGER
//config:	bool "mem_debugger"
//config:	default y

//applet:APPLET(mem_debugger, BB_DIR_USR_BIN, BB_SUID_DROP)

//kbuild:lib-y += mem_debugger.o

//usage:#define mem_debugger_trivial_usage
//usage:       ""
//usage:#define mem_debugger_full_usage "\n\n"
//usage:       "Interactive memory debugging ioctl utility"

#include "libbb.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

#define DEVICE_NAME "mem_debugger"

#define MEM_SHOW_FREE_AREAS _IO('M', 1)
#define COMPOUND_PAGE_TEST  _IO('M', 2)
#define MEM_DUMP_VMAS       _IO('M', 3)
#define MEM_DUMP_NUMA	    _IO('M', 4)
#define MEM_FILECACHE_DUMP  _IO('M', 5)

int mem_debugger_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;

int self_proc(const char * msg, const char * path) {
        int fd, n;
        static char buf[256];
        /* Open /proc/self/maps using BusyBox's error-checking opener */
        fd = open(path, O_RDONLY);
	if (fd < 0) {
		printf("Error while opening the %s\n", path);
		return -1;
	}

        printf("%s", msg);
        fflush(stdout);

        while ((n = read(fd, buf, 256)) > 0) {
                write(1, buf, n);
        }

        /* Close the file descriptor safely (optional but good practice) */
        close(fd);
	return 0;
}

int mem_debugger_main(int argc, char **argv)
{
	int fd;
	int choice;
	int ret;
	char buf[32];

	(void)argc;
	(void)argv;

	fd = xopen("/dev/" DEVICE_NAME, O_RDWR);

	printf("Memory Debugger v0.1\n");

	for (;;) {
		printf("\n");
		printf("1. Show Free Areas\n");
		printf("2. Compound Page Test\n");
		printf("3. Dump all vma's userspace + kernel(shared)\n");
		printf("4. Dump all numa nodes\n");
		printf("5. Dump page cache (aka:file cache)\n");
		printf("6. Dump /proc/self/maps\n");
		printf("0. Exit\n");
		printf("Select: ");
		fflush(stdout);

		if (!fgets(buf, sizeof(buf), stdin))
			break;

		choice = atoi(buf);

		switch (choice) {
		case 1:
			ret = ioctl(fd, MEM_SHOW_FREE_AREAS);

			if (ret < 0)
				printf("MEM_SHOW_FREE_AREAS failed: %s\n",
				       strerror(errno));
			else
				printf("MEM_SHOW_FREE_AREAS returned %d\n", ret);
			break;

		case 2:
			ret = ioctl(fd, COMPOUND_PAGE_TEST);

			if (ret < 0)
				printf("COMPOUND_PAGE_TEST failed: %s\n",
				       strerror(errno));
			else
				printf("COMPOUND_PAGE_TEST returned %d\n", ret);
			break;
                case 3: // New VMA Dump Option
                        ret = ioctl(fd, MEM_DUMP_VMAS);

                        if (ret < 0)
                                printf("MEM_DUMP_VMAS failed: %s\n", strerror(errno));
                        else
                                printf("MEM_DUMP_VMAS executed successfully. Check dmesg.\n");
                        break;
		case 4:
			ret = ioctl(fd, MEM_DUMP_NUMA);
			if (ret < 0) {
				printf("MEM_DUMP_NUMA, failed\n");
			} else {
				printf("MEM_DUMP_NUMA, returned.\n");
			}
			break;
                case 5:
                        ret = ioctl(fd, MEM_FILECACHE_DUMP);
                        if (ret < 0) {
                                printf("MEM_FILECACHE_DUMP, failed\n");
                        } else {
                                printf("MEM_FILECACHE_DUMP, returned.\n");
                        }
                        break;
		case 6:
			/* TODO: we should print all the /proc/self path and
			 * 	 ask user to chose which one to pick
			 */
			self_proc("reading /proc/self/maps", "/proc/self/maps");
			break;


		case 0:
			close(fd);
			return 0;

		default:
			printf("Invalid selection\n");
			break;
		}
	}

	close(fd);
	return 0;
}
