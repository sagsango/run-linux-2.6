/*
 * Set up the VMAs to tell the VM about the vDSO.
 * Copyright 2007 Andi Kleen, SUSE Labs.
 * Subject to the GPL, v.2
 */
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/random.h>
#include <linux/elf.h>
#include <asm/vsyscall.h>
#include <asm/vgtod.h>
#include <asm/proto.h>
#include <asm/vdso.h>

#include "vextern.h"		/* Just for VMAGIC.  */
#undef VEXTERN

unsigned int __read_mostly vdso_enabled = 1;

extern char vdso_start[], vdso_end[];
extern unsigned short vdso_sync_cpuid;

static struct page **vdso_pages;
static unsigned vdso_size;

static inline void *var_ref(void *p, char *name)
{
	if (*(void **)p != (void *)VMAGIC) {
		printk("VDSO: variable %s broken\n", name);
		vdso_enabled = 0;
	}
	return p;
}

/* XXX: vdso pages init */
static int __init init_vdso_vars(void)
{
	int npages = (vdso_end - vdso_start + PAGE_SIZE - 1) / PAGE_SIZE;
	int i;
	char *vbase;

	vdso_size = npages << PAGE_SHIFT;
	vdso_pages = kmalloc(sizeof(struct page *) * npages, GFP_KERNEL);
	if (!vdso_pages)
		goto oom;
	for (i = 0; i < npages; i++) {
		struct page *p;
		p = alloc_page(GFP_KERNEL);
		if (!p)
			goto oom;
		vdso_pages[i] = p;
		copy_page(page_address(p), vdso_start + i*PAGE_SIZE);
	}

	vbase = vmap(vdso_pages, npages, 0, PAGE_KERNEL);
	if (!vbase)
		goto oom;

	if (memcmp(vbase, "\177ELF", 4)) {
		printk("VDSO: I'm broken; not ELF\n");
		vdso_enabled = 0;
	}

#define VEXTERN(x) \
	*(typeof(__ ## x) **) var_ref(VDSO64_SYMBOL(vbase, x), #x) = &__ ## x;
#include "vextern.h"
#undef VEXTERN
	vunmap(vbase);
	return 0;

 oom:
	printk("Cannot allocate vdso\n");
	vdso_enabled = 0;
	return -ENOMEM;
}
subsys_initcall(init_vdso_vars);

struct linux_binprm;

/* Put the vdso above the (randomized) stack with another randomized offset.
   This way there is no hole in the middle of address space.
   To save memory make sure it is still in the same PTE as the stack top.
   This doesn't give that many random bits */
static unsigned long vdso_addr(unsigned long start, unsigned len)
{
	unsigned long addr, end;
	unsigned offset;
	end = (start + PMD_SIZE - 1) & PMD_MASK;
	if (end >= TASK_SIZE_MAX)
		end = TASK_SIZE_MAX;
	end -= len;
	/* This loses some more bits than a modulo, but is cheaper */
	offset = get_random_int() & (PTRS_PER_PTE - 1);
	addr = start + (offset << PAGE_SHIFT);
	if (addr >= end)
		addr = end;
	return addr;
}

/* XXX: vdso mapping get installed runtime on every exec
 Thread 2 hit Breakpoint 1, install_special_mapping (mm=mm@entry=0xffff88001f19b740, addr=140733885444096, len=4096, vm_flags=vm_flags@entry=67108981, pages=0xffff88005f002408) at mm/mmap.c:2485
2485	{
(gdb) bt
#0  install_special_mapping (mm=mm@entry=0xffff88001f19b740, addr=140733885444096, len=4096, vm_flags=vm_flags@entry=67108981, pages=0xffff88005f002408) at mm/mmap.c:2485
#1  0xffffffff810282c3 in arch_setup_additional_pages (bprm=bprm@entry=0xffff88001ead6600, uses_interp=<optimized out>) at arch/x86/vdso/vma.c:123
#2  0xffffffff81114996 in load_elf_binary (bprm=0xffff88001ead6600, regs=0xffff88001eacff58) at fs/binfmt_elf.c:921
#3  0xffffffff810dfd55 in search_binary_handler (bprm=bprm@entry=0xffff88001ead6600, regs=regs@entry=0xffff88001eacff58) at fs/exec.c:1331
#4  0xffffffff810e0f69 in do_execve (filename=filename@entry=0xffff88001eab8000 "/bin/ls", argv=argv@entry=0x8a9c10, envp=envp@entry=0x8a9c20, regs=regs@entry=0xffff88001eacff58) at fs/exec.c:1452
#5  0xffffffff810083c9 in sys_execve (name=<optimized out>, argv=0x8a9c10, envp=0x8a9c20, regs=0xffff88001eacff58) at arch/x86/kernel/process.c:317
#6  0xffffffff8153a99c in stub_execve () at arch/x86/kernel/entry_64.S:716
#7  0x000000000049c9b7 in ?? ()
Backtrace stopped: previous frame inner to this frame (corrupt stack?)
(gdb) p vdso_pages
$1 = (struct page **) 0xffff88005f002408
(gdb)
*/
/* Setup a VMA at program startup for the vsyscall page.
   Not called for compat tasks */
int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr;
	int ret;

	if (!vdso_enabled)
		return 0;

	down_write(&mm->mmap_sem);
	addr = vdso_addr(mm->start_stack, vdso_size);
	addr = get_unmapped_area(NULL, addr, vdso_size, 0, 0);
	if (IS_ERR_VALUE(addr)) {
		ret = addr;
		goto up_fail;
	}

	current->mm->context.vdso = (void *)addr;

	ret = install_special_mapping(mm, addr, vdso_size,
				      VM_READ|VM_EXEC|
				      VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC|
				      VM_ALWAYSDUMP,
				      vdso_pages);
	if (ret) {
		current->mm->context.vdso = NULL;
		goto up_fail;
	}

up_fail:
	up_write(&mm->mmap_sem);
	return ret;
}

static __init int vdso_setup(char *s)
{
	vdso_enabled = simple_strtoul(s, NULL, 0);
	return 0;
}
__setup("vdso=", vdso_setup);
