//config:config FOLSOM
//config:	bool "folsom"
//config:	default y
//config:	help
//config:	Opens a file and mmaps specific offsets in 4K blocks.

//applet:IF_FOLSOM(APPLET(folsom, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_FOLSOM) += folsom.o

//usage:#define folsom_trivial_usage
//usage:       "FILE OFFSET [OFFSET...]"
//usage:#define folsom_full_usage "\n\n"
//usage:       "Open FILE and mmap 4K blocks at specified memory offsets"

#include "libbb.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define EXT3_IOC_ADDRESS_SPACE    _IO('f', 20)



#define N_READ 1
#define N_WRITE 1


void memstat(const char * msg) {
	return;
	int statm_fd = open("/proc/self/statm", O_RDONLY);
	if (statm_fd >= 0) {
		static char statm_buf[128];
		int n = read(statm_fd, statm_buf, sizeof(statm_buf) - 1);
		if (n > 0) {
			statm_buf[n] = '\0';
			printf("%s [RAW STATM PAGES]: %s", msg, statm_buf);
		}
		close(statm_fd);
		fflush(stdout);
	}
}

int folsom_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int folsom_main(int argc, char **argv)
{
	int fd;
	int i, j;
	const char *filename;

	// 1. Ensure minimal arguments (folsom filename offset1 [offset2 ...])
	if (argc < 3) {
		bb_show_usage();
	}

	filename = argv[1];

	// 2. Open the file safely using BusyBox helper utilities
	fd = xopen(filename, O_RDWR);

	static void * addr[256];

	printf("Version - 0.1\n");
	fflush(stdout);

	memstat("after start");

	// 3. Iterate through all offset arguments provided
	for (i = 2; i < argc; i++) {
		off_t offset;

		// Convert string argument to a long numeric offset safely
		offset = (off_t)bb_strtoul(argv[i], NULL, 0);
		if (errno) {
			bb_error_msg("invalid offset: %s", argv[i]);
			continue;
		}

		memstat("before mmap");
		// 4. Mmap a 4KB chunk at the designated offset
		addr[i] = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
		memstat("after mmap");
		
		if (addr[i] == MAP_FAILED) {
			bb_perror_msg("mmap failed for offset %lld", (long long)offset);
		} else {
			printf("Successfully mapped 4K block at offset %lld to address %p\n", 
			       (long long)offset, addr[i]);
			fflush(stdout);
			
			// Optional: Clean up and unmap the memory block
			// munmap(addr, 4096); // TODO: unmap all
		}
	}

	

	static long read_offsets[N_READ] = {0};
	static long write_offset[N_WRITE] = {0};

	for (j=2; j<argc; ++j) {
		char * buf = (char*)addr[j];

		for (i=0; i<N_READ; ++i) {
			memstat("before read");
			char c = buf[i];
			printf("Read char at offset [%d] = %c\n", read_offsets[i], c);
			fflush(stdout);
			memstat("after read");

		}

		for (i=0; i<N_WRITE; ++i) {
			memstat("before write");
			char c = 'a' + i;
			buf[i] = c;
			printf("Wrote char %c at offset = %d\n", c, write_offset[i]);
			fflush(stdout);
			memstat("after write");
		}
	}


	if (ioctl(fd, EXT3_IOC_ADDRESS_SPACE, 0) < 0) {
		perror("[-] ioctl failed");
		return 1;
	}

	printf("Done\n");
	fflush(stdout);

	close(fd);
	for (i=2; i<argc; ++i) {
		munmap(addr[i], 4096);
	}
	memstat("before exit");
	return 0;
}

