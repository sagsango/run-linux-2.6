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
int folsom_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int folsom_main(int argc, char **argv)
{
	int fd;
	int i;
	const char *filename;

	// 1. Ensure minimal arguments (folsom filename offset1 [offset2 ...])
	if (argc < 3) {
		bb_show_usage();
	}

	filename = argv[1];

	// 2. Open the file safely using BusyBox helper utilities
	fd = xopen(filename, O_RDWR);

	void * addr;

	printf("Version - 0.1\n");
	fflush(stdout);

	// 3. Iterate through all offset arguments provided
	for (i = 2; i < argc; i++) {
		off_t offset;

		// Convert string argument to a long numeric offset safely
		offset = (off_t)bb_strtoul(argv[i], NULL, 0);
		if (errno) {
			bb_error_msg("invalid offset: %s", argv[i]);
			continue;
		}

		// 4. Mmap a 4KB chunk at the designated offset
		addr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
		
		if (addr == MAP_FAILED) {
			bb_perror_msg("mmap failed for offset %lld", (long long)offset);
		} else {
			printf("Successfully mapped 4K block at offset %lld to address %p\n", 
			       (long long)offset, addr);
			fflush(stdout);
			
			// Optional: Clean up and unmap the memory block
			// munmap(addr, 4096); // TODO: unmap all
		}
	}

	

	long read_offsets[N_READ] = {0};
	long write_offset[N_WRITE] = {0};

	char * buf = (char*)addr;

	for (i=0; i<N_READ; ++i) {
		char c = buf[i];
		printf("Read char at offset [%d] = %c\n", read_offsets[i], c);
		fflush(stdout);
	}

	for (i=0; i<N_WRITE; ++i) {
		char c = 'a' + i;
		buf[i] = c;
		printf("Wrote char %c at offset = %d\n", c, write_offset[i]);
		fflush(stdout);
	}


	if (ioctl(fd, EXT3_IOC_ADDRESS_SPACE, 0) < 0) {
		perror("[-] ioctl failed");
		return 1;
	}

	printf("Done\n");
	fflush(stdout);

	close(fd);
	return 0;
}

