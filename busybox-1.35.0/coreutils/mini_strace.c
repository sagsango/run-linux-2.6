//config:config MINI_STRACE
//config:       bool "mini_strace"
//config:       default y
//config:       help
//config:       Minimal strace-like syscall/register/stack tracer

//applet:IF_MINI_STRACE(APPLET(mini_strace, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_MINI_STRACE) += mini_strace.o

//usage:#define mini_strace_trivial_usage
//usage:       "PROGRAM [ARGS...]"
//usage:#define mini_strace_full_usage "\n\n"
//usage:       "Trace system calls, registers and stack"

#include "libbb.h"

#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/user.h>

#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
 * ============================================================
 * x86-64 Linux syscall table
 * ============================================================
 */

struct syscall_info {
	int nr;
	const char *name;
	int nargs;
};


static const struct syscall_info syscall_table[] = {
	{ 0,   "read",             3 },
	{ 1,   "write",            3 },
	{ 2,   "open",             3 },
	{ 3,   "close",            1 },
	{ 4,   "stat",             2 },
	{ 5,   "fstat",            2 },
	{ 6,   "lstat",            2 },
	{ 7,   "poll",             3 },
	{ 8,   "lseek",            3 },
	{ 9,   "mmap",             6 },
	{ 10,  "mprotect",         3 },
	{ 11,  "munmap",            2 },
	{ 12,  "brk",               1 },
	{ 13,  "rt_sigaction",      4 },
	{ 14,  "rt_sigprocmask",    4 },
	{ 16,  "ioctl",             3 },
	{ 17,  "pread64",            4 },
	{ 18,  "pwrite64",           4 },
	{ 19,  "readv",              3 },
	{ 20,  "writev",             3 },
	{ 21,  "access",             2 },
	{ 22,  "pipe",               1 },
	{ 32,  "dup",                1 },
	{ 33,  "dup2",               2 },
	{ 35,  "nanosleep",           2 },
	{ 39,  "getpid",              0 },
	{ 40,  "sendfile",            4 },
	{ 41,  "socket",              3 },
	{ 42,  "connect",             3 },
	{ 43,  "accept",              3 },
	{ 44,  "sendto",              6 },
	{ 45,  "recvfrom",            6 },
	{ 46,  "sendmsg",             3 },
	{ 47,  "recvmsg",             3 },
	{ 48,  "shutdown",             2 },
	{ 49,  "bind",                 3 },
	{ 50,  "listen",               2 },
	{ 51,  "getsockname",          3 },
	{ 52,  "getpeername",          3 },
	{ 53,  "socketpair",           4 },
	{ 54,  "setsockopt",            5 },
	{ 55,  "getsockopt",            5 },
	{ 56,  "clone",                5 },
	{ 57,  "fork",                 0 },
	{ 58,  "vfork",                0 },
	{ 59,  "execve",               3 },
	{ 60,  "exit",                 1 },
	{ 61,  "wait4",                4 },
	{ 62,  "kill",                 2 },
	{ 63,  "uname",                1 },
	{ 72,  "fcntl",                3 },
	{ 78,  "getdents",             3 },
	{ 79,  "getcwd",               2 },
	{ 80,  "chdir",                1 },
	{ 83,  "mkdir",                2 },
	{ 87,  "unlink",               1 },
	{ 89,  "readlink",              3 },
	{ 90,  "chmod",                 2 },
	{ 91,  "fchmod",                2 },
	{ 92,  "chown",                 3 },
	{ 93,  "fchown",                3 },
	{ 97,  "getrlimit",             2 },
	{ 99,  "sysinfo",               1 },
	{ 102, "getuid",                0 },
	{ 104, "getgid",                0 },
	{ 107, "geteuid",               0 },
	{ 108, "getegid",               0 },
	{ 110, "getppid",               0 },
	{ 131, "sigaltstack",            2 },
	{ 158, "arch_prctl",             2 },
	{ 186, "gettid",                0 },
	{ 202, "futex",                 6 },
	{ 217, "getdents64",             3 },
	{ 228, "clock_gettime",          2 },
	{ 231, "exit_group",             1 },
	{ 232, "epoll_wait",             4 },
	{ 233, "epoll_ctl",              4 },
	{ 257, "openat",                 4 },
	{ 262, "newfstatat",             4 },
	{ 273, "set_robust_list",        2 },
	{ 302, "prlimit64",              4 },
	{ 318, "getrandom",              3 },
	{ 321, "bpf",                    3 },
	{ 322, "execveat",               5 },
	{ 228, "clock_gettime",          2 },
	{ -1,  NULL,                     0 }
};


static const struct syscall_info *find_syscall(long nr)
{
	int i;

	for (i = 0; syscall_table[i].nr != -1; i++) {
		if (syscall_table[i].nr == nr)
			return &syscall_table[i];
	}

	return NULL;
}


/*
 * ============================================================
 * Read one 64-bit word from child memory
 * ============================================================
 */

static unsigned long long peek_word(pid_t pid,
				    unsigned long long addr)
{
	unsigned long value;

	errno = 0;

	value = ptrace(
		PTRACE_PEEKDATA,
		pid,
		(void *)(unsigned long)addr,
		NULL
	);

	if (errno)
		return 0;

	return (unsigned long long)value;
}


/*
 * ============================================================
 * Read string from traced process
 * ============================================================
 */

static void read_child_string(pid_t pid,
			      unsigned long long addr,
			      char *buf,
			      size_t size)
{
	size_t pos = 0;

	if (!addr) {
		snprintf(buf, size, "NULL");
		return;
	}

	while (pos < size - 1) {

		unsigned long long word;
		int i;

		word = peek_word(pid, addr + pos);

		for (i = 0;
		     i < 8 && pos < size - 1;
		     i++) {

			char c;

			c = (word >> (i * 8)) & 0xff;

			if (c == '\0') {
				buf[pos] = '\0';
				return;
			}

			if (c < 32 || c > 126)
				c = '.';

			buf[pos++] = c;
		}
	}

	buf[pos] = '\0';
}


/*
 * ============================================================
 * Print registers
 * ============================================================
 */

static void print_registers(struct user_regs_struct *r)
{
	printf("    REGISTERS:\n");

	printf("      RAX      = 0x%016llx\n",
	       (unsigned long long)r->rax);

	printf("      RBX      = 0x%016llx\n",
	       (unsigned long long)r->rbx);

	printf("      RCX      = 0x%016llx\n",
	       (unsigned long long)r->rcx);

	printf("      RDX      = 0x%016llx\n",
	       (unsigned long long)r->rdx);

	printf("      RSI      = 0x%016llx\n",
	       (unsigned long long)r->rsi);

	printf("      RDI      = 0x%016llx\n",
	       (unsigned long long)r->rdi);

	printf("      RBP      = 0x%016llx\n",
	       (unsigned long long)r->rbp);

	printf("      RSP      = 0x%016llx\n",
	       (unsigned long long)r->rsp);

	printf("      R8       = 0x%016llx\n",
	       (unsigned long long)r->r8);

	printf("      R9       = 0x%016llx\n",
	       (unsigned long long)r->r9);

	printf("      R10      = 0x%016llx\n",
	       (unsigned long long)r->r10);

	printf("      R11      = 0x%016llx\n",
	       (unsigned long long)r->r11);

	printf("      R12      = 0x%016llx\n",
	       (unsigned long long)r->r12);

	printf("      R13      = 0x%016llx\n",
	       (unsigned long long)r->r13);

	printf("      R14      = 0x%016llx\n",
	       (unsigned long long)r->r14);

	printf("      R15      = 0x%016llx\n",
	       (unsigned long long)r->r15);

	printf("      RIP      = 0x%016llx\n",
	       (unsigned long long)r->rip);

	printf("      RSP      = 0x%016llx\n",
	       (unsigned long long)r->rsp);

	printf("      RFLAGS   = 0x%016llx\n",
	       (unsigned long long)r->eflags);

	printf("      ORIG_RAX = 0x%016llx\n",
	       (unsigned long long)r->orig_rax);
}


/*
 * ============================================================
 * Dump user stack
 * ============================================================
 */

static void print_stack(pid_t pid,
			struct user_regs_struct *r)
{
	int i;

	printf("    STACK @ RSP=0x%016llx:\n",
	       (unsigned long long)r->rsp);

	/*
	 * Dump 128 bytes.
	 *
	 * x86-64 word = 8 bytes.
	 */

	for (i = 0; i < 16; i++) {

		unsigned long long addr;
		unsigned long long value;

		addr = (unsigned long long)r->rsp +
		       (i * 8);

		value = peek_word(pid, addr);

		printf("      [RSP+0x%02x] "
		       "0x%016llx : 0x%016llx\n",
		       i * 8,
		       addr,
		       value);
	}
}


/*
 * ============================================================
 * Print syscall arguments
 *
 * x86-64 Linux syscall ABI:
 *
 * RAX = syscall number
 *
 * RDI = arg1
 * RSI = arg2
 * RDX = arg3
 * R10 = arg4
 * R8  = arg5
 * R9  = arg6
 * ============================================================
 */

static void print_arguments(pid_t pid,
			    long nr,
			    struct user_regs_struct *r)
{
	const struct syscall_info *info;
	char string[256];

	info = find_syscall(nr);

	printf("    ARGUMENTS:\n");

	if (!info) {

		printf("      RDI = 0x%016llx\n",
		       (unsigned long long)r->rdi);

		printf("      RSI = 0x%016llx\n",
		       (unsigned long long)r->rsi);

		printf("      RDX = 0x%016llx\n",
		       (unsigned long long)r->rdx);

		printf("      R10 = 0x%016llx\n",
		       (unsigned long long)r->r10);

		printf("      R8  = 0x%016llx\n",
		       (unsigned long long)r->r8);

		printf("      R9  = 0x%016llx\n",
		       (unsigned long long)r->r9);

		return;
	}


	if (info->nargs >= 1)
		printf("      arg1 = 0x%016llx\n",
		       (unsigned long long)r->rdi);

	if (info->nargs >= 2)
		printf("      arg2 = 0x%016llx\n",
		       (unsigned long long)r->rsi);

	if (info->nargs >= 3)
		printf("      arg3 = 0x%016llx\n",
		       (unsigned long long)r->rdx);

	if (info->nargs >= 4)
		printf("      arg4 = 0x%016llx\n",
		       (unsigned long long)r->r10);

	if (info->nargs >= 5)
		printf("      arg5 = 0x%016llx\n",
		       (unsigned long long)r->r8);

	if (info->nargs >= 6)
		printf("      arg6 = 0x%016llx\n",
		       (unsigned long long)r->r9);


	/*
	 * --------------------------------------------------------
	 * open(pathname, flags, mode)
	 * --------------------------------------------------------
	 */

	if (nr == 2) {

		read_child_string(
			pid,
			(unsigned long long)r->rdi,
			string,
			sizeof(string)
		);

		printf("      pathname = \"%s\"\n",
		       string);

		printf("      flags = 0x%016llx\n",
		       (unsigned long long)r->rsi);

		printf("      mode = 0x%016llx\n",
		       (unsigned long long)r->rdx);
	}


	/*
	 * --------------------------------------------------------
	 * openat(dirfd, pathname, flags, mode)
	 * --------------------------------------------------------
	 */

	if (nr == 257) {

		read_child_string(
			pid,
			(unsigned long long)r->rsi,
			string,
			sizeof(string)
		);

		printf("      dirfd = %lld\n",
		       (long long)r->rdi);

		printf("      pathname = \"%s\"\n",
		       string);

		printf("      flags = 0x%016llx\n",
		       (unsigned long long)r->rdx);

		printf("      mode = 0x%016llx\n",
		       (unsigned long long)r->r10);
	}


	/*
	 * --------------------------------------------------------
	 * execve(filename, argv, envp)
	 * --------------------------------------------------------
	 */

	if (nr == 59) {

		read_child_string(
			pid,
			(unsigned long long)r->rdi,
			string,
			sizeof(string)
		);

		printf("      filename = \"%s\"\n",
		       string);

		printf("      argv = 0x%016llx\n",
		       (unsigned long long)r->rsi);

		printf("      envp = 0x%016llx\n",
		       (unsigned long long)r->rdx);
	}


	/*
	 * --------------------------------------------------------
	 * read(fd, buffer, count)
	 * --------------------------------------------------------
	 */

	if (nr == 0) {

		printf("      fd = %lld\n",
		       (long long)r->rdi);

		printf("      buffer = 0x%016llx\n",
		       (unsigned long long)r->rsi);

		printf("      count = %llu\n",
		       (unsigned long long)r->rdx);
	}


	/*
	 * --------------------------------------------------------
	 * write(fd, buffer, count)
	 * --------------------------------------------------------
	 */

	if (nr == 1) {

		printf("      fd = %lld\n",
		       (long long)r->rdi);

		printf("      buffer = 0x%016llx\n",
		       (unsigned long long)r->rsi);

		printf("      count = %llu\n",
		       (unsigned long long)r->rdx);
	}


	/*
	 * --------------------------------------------------------
	 * lseek(fd, offset, whence)
	 * --------------------------------------------------------
	 */

	if (nr == 8) {

		printf("      fd = %lld\n",
		       (long long)r->rdi);

		printf("      offset = 0x%016llx\n",
		       (unsigned long long)r->rsi);

		printf("      whence = %lld\n",
		       (long long)r->rdx);
	}


	/*
	 * --------------------------------------------------------
	 * mmap(addr, length, prot, flags, fd, offset)
	 * --------------------------------------------------------
	 */

	if (nr == 9) {

		printf("      addr = 0x%016llx\n",
		       (unsigned long long)r->rdi);

		printf("      length = %llu\n",
		       (unsigned long long)r->rsi);

		printf("      prot = 0x%llx\n",
		       (unsigned long long)r->rdx);

		printf("      flags = 0x%llx\n",
		       (unsigned long long)r->r10);

		printf("      fd = %lld\n",
		       (long long)r->r8);

		printf("      offset = 0x%016llx\n",
		       (unsigned long long)r->r9);
	}
}


/*
 * ============================================================
 * Print syscall return value
 * ============================================================
 */

static void print_return_value(
	struct user_regs_struct *r)
{
	long long ret;

	ret = (long long)r->rax;

	if (ret < 0 && ret >= -4095) {

		printf("    RETURN = %lld\n", ret);

		printf("    ERROR  = errno=%lld\n",
		       -ret);

		switch (-ret) {

		case 1:
			printf("             EPERM\n");
			break;

		case 2:
			printf("             ENOENT\n");
			break;

		case 3:
			printf("             ESRCH\n");
			break;

		case 5:
			printf("             EIO\n");
			break;

		case 9:
			printf("             EBADF\n");
			break;

		case 13:
			printf("             EACCES\n");
			break;

		case 14:
			printf("             EFAULT\n");
			break;

		case 22:
			printf("             EINVAL\n");
			break;

		case 38:
			printf("             ENOSYS\n");
			break;

		default:
			printf("             unknown\n");
			break;
		}

	} else {

		printf("    RETURN = %lld "
		       "(0x%016llx)\n",
		       ret,
		       (unsigned long long)r->rax);
	}
}


/*
 * ============================================================
 * Main
 * ============================================================
 */

int mini_strace_main(int argc, char **argv)
	MAIN_EXTERNALLY_VISIBLE;

int mini_strace_main(int argc, char **argv)
{
	pid_t pid;
	int status;
	int entering = 1;

	if (argc < 2)
		bb_show_usage();


	/*
	 * --------------------------------------------------------
	 * Fork
	 * --------------------------------------------------------
	 */

	pid = fork();

	if (pid < 0)
		bb_perror_msg_and_die("fork");


	/*
	 * --------------------------------------------------------
	 * Child
	 * --------------------------------------------------------
	 */

	if (pid == 0) {

		if (ptrace(PTRACE_TRACEME,
			   0,
			   NULL,
			   NULL) < 0) {

			bb_perror_msg_and_die(
				"ptrace(TRACEME)");
		}

		/*
		 * Stop child.
		 */

		raise(SIGSTOP);

		/*
		 * Execute target.
		 */

		execvp(argv[1], &argv[1]);

		bb_perror_msg_and_die(
			"exec %s",
			argv[1]);
	}


	/*
	 * --------------------------------------------------------
	 * Parent
	 * --------------------------------------------------------
	 */

	printf("mini_strace: tracing PID %d\n",
	       pid);


	/*
	 * Wait for child's SIGSTOP.
	 */

	if (waitpid(pid, &status, 0) < 0)
		bb_perror_msg_and_die("waitpid");


	/*
	 * Start syscall tracing.
	 */

	if (ptrace(PTRACE_SYSCALL,
		   pid,
		   NULL,
		   NULL) < 0) {

		bb_perror_msg_and_die(
			"ptrace(SYSCALL)");
	}


	/*
	 * --------------------------------------------------------
	 * Trace loop
	 * --------------------------------------------------------
	 */

	while (1) {

		struct user_regs_struct regs;
		long nr;


		/*
		 * Wait for syscall stop.
		 */

		if (waitpid(pid, &status, 0) < 0) {

			if (errno == EINTR)
				continue;

			bb_perror_msg_and_die("waitpid");
		}


		/*
		 * Process exited.
		 */

		if (WIFEXITED(status)) {

			printf("\nPROCESS EXITED: %d\n",
			       WEXITSTATUS(status));

			break;
		}


		/*
		 * Process killed.
		 */

		if (WIFSIGNALED(status)) {

			printf("\nPROCESS SIGNALED: %d\n",
			       WTERMSIG(status));

			break;
		}


		if (!WIFSTOPPED(status))
			continue;


		/*
		 * Get child registers.
		 */

		if (ptrace(PTRACE_GETREGS,
			   pid,
			   NULL,
			   &regs) < 0) {

			bb_perror_msg_and_die(
				"ptrace(GETREGS)");
		}


		/*
		 * ====================================================
		 * SYSCALL ENTRY
		 * ====================================================
		 */

		if (entering) {

			const struct syscall_info *info;

			/*
			 * IMPORTANT:
			 *
			 * On x86-64:
			 *
			 * orig_rax = syscall number
			 * rax      = return value
			 */

			nr = (long)regs.orig_rax;

			info = find_syscall(nr);

			printf("\n");
			printf("==================================================\n");

			printf("SYSCALL ENTRY\n");

			printf("  NR   = %ld\n", nr);

			printf("  NAME = %s\n",
			       info ? info->name : "unknown");

			print_arguments(
				pid,
				nr,
				&regs
			);

			print_registers(&regs);

			print_stack(
				pid,
				&regs
			);

			entering = 0;
		}


		/*
		 * ====================================================
		 * SYSCALL EXIT
		 * ====================================================
		 */

		else {

			printf("SYSCALL EXIT\n");

			/*
			 * RAX now contains the return value.
			 */

			print_return_value(&regs);

			printf("    RAX = 0x%016llx\n",
			       (unsigned long long)regs.rax);

			entering = 1;
		}


		/*
		 * Continue to next syscall.
		 */

		if (ptrace(PTRACE_SYSCALL,
			   pid,
			   NULL,
			   NULL) < 0) {

			bb_perror_msg_and_die(
				"ptrace(SYSCALL)");
		}
	}


	return 0;
}

