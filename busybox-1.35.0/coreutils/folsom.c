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
#include <sys/syscall.h> /* Gives access to direct platform __NR_xxxx numbers */



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

void self_maps(const char * msg) {
	int fd, n;
	static char buf[256];
	/* Open /proc/self/maps using BusyBox's error-checking opener */
	fd = open("/proc/self/maps", O_RDONLY);


	printf("%s", msg);
	fflush(stdout);

	while ((n = read(fd, buf, 256)) > 0) {
		write(1, buf, n);
	}

	/* Close the file descriptor safely (optional but good practice) */
	close(fd);

}
void anon_mapping() {
	self_maps("before anon_mapping mmap()\n");

	char *addr;
	size_t page_size = 4096; /* Architecture default standard page size */
	size_t total_len = page_size * 2;

	/*
	 * 1. Allocate 2 anonymous pages using explicit raw kernel syscall boundary wrappers.
	 * Pass NULL so the kernel selects the virtual memory location.
	 * File descriptor must be -1 and offset must be 0 for anonymous maps.
	 */
	//addr = (char *)syscall(__NR_mmap, NULL, total_len,
	//                       PROT_READ | PROT_WRITE,
	//                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	addr = mmap(NULL, total_len,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS,
			0,
			0);

	/* Check if the system call vector returned MAP_FAILED */
	if (addr == MAP_FAILED) {
		syscall(__NR_write, STDERR_FILENO, "Syscall mmap failed\n", 20);
		return EXIT_FAILURE;
	} else {

		printf("anon mapping successful - [%p - %p]\n", addr, (long)addr + page_size); 

	}

	/*
	 * 2. Access and write to the first address of Page 1.
	 * This triggers a physical page fault inside the kernel MMU.
	 */
	char *page1_start = addr;
	page1_start[0] = 'H';
	page1_start[1] = 'i';
	page1_start[2] = ' ';
	page1_start[3] = 'P';
	page1_start[4] = 'a';
	page1_start[5] = 'g';
	page1_start[6] = 'e';
	page1_start[7] = '1';
	page1_start[8] = '\n';

	/*
	 * 3. Calculate address offset and write to the first address of Page 2.
	 * This triggers an independent second page fault allocation.
	 */
	char *page2_start = addr + page_size;
	page2_start[0] = 'H';
	page2_start[1] = 'i';
	page2_start[2] = ' ';
	page2_start[3] = 'P';
	page2_start[4] = 'a';
	page2_start[5] = 'g';
	page2_start[6] = 'e';
	page2_start[7] = '2';
	page2_start[8] = '\n';


	/*
	 * 4. Use system call vectors directly to write the contents out to stdout.
	 * Standard printf/write abstractions are totally bypassed here.
	 */
	syscall(__NR_write, STDOUT_FILENO, page1_start, 9);
	syscall(__NR_write, STDOUT_FILENO, page2_start, 9);


	self_maps("after anon_mappings map() & before unmap()\n");

	/* 5. Free the 2 pages cleanly via raw munmap syscall wrapper */
	syscall(__NR_munmap, addr, total_len);

	self_maps("after anaon_mappings unmap()\n");
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

	self_maps("after start\n");
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


	self_maps("before munmap\n");

	printf("Done\n");
	fflush(stdout);

	close(fd);
	for (i=2; i<argc; ++i) {
		munmap(addr[i], 4096);
	}
	self_maps("after unmap; before exit\n");
	memstat("before exit");


	anon_mapping();
	return 0;
}

