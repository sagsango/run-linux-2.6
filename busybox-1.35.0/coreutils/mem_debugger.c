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

int mem_debugger_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;

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
