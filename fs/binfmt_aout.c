/*
 *  linux/fs/binfmt_aout.c
 *
 *  Copyright (C) 1991, 1992, 1996  Linus Torvalds
 */

#include <linux/module.h>

#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/a.out.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/init.h>
#include <linux/coredump.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/a.out-core.h>

static int load_aout_binary(struct linux_binprm *, struct pt_regs * regs);
static int load_aout_library(struct file*);
static int aout_core_dump(struct coredump_params *cprm);

static struct linux_binfmt aout_format = {
	.module		= THIS_MODULE,
	.load_binary	= load_aout_binary,
	.load_shlib	= load_aout_library,
	.core_dump	= aout_core_dump,
	.min_coredump	= PAGE_SIZE
};

#define BAD_ADDR(x)	((unsigned long)(x) >= TASK_SIZE)

static int set_brk(unsigned long start, unsigned long end)
{
	start = PAGE_ALIGN(start);
	end = PAGE_ALIGN(end);
	if (end > start) {
		unsigned long addr;
		addr = vm_brk(start, end - start);
		if (BAD_ADDR(addr))
			return addr;
	}
	return 0;
}


static int aout_core_dump(struct coredump_params *cprm)
{
	struct file *file = cprm->file;
	mm_segment_t fs;
	int has_dumped = 0;
	void __user *dump_start;
	int dump_size;
	struct user dump;
#ifdef __alpha__
#       define START_DATA(u)	((void __user *)u.start_data)
#else
#	define START_DATA(u)	((void __user *)((u.u_tsize << PAGE_SHIFT) + \
				 u.start_code))
#endif
#       define START_STACK(u)   ((void __user *)u.start_stack)

	fs = get_fs();
	set_fs(KERNEL_DS);
	has_dumped = 1;
	current->flags |= PF_DUMPCORE;
       	strncpy(dump.u_comm, current->comm, sizeof(dump.u_comm));
	dump.u_ar0 = offsetof(struct user, regs);
	dump.signal = cprm->signr;
	aout_dump_thread(cprm->regs, &dump);

	if ((dump.u_dsize + dump.u_ssize+1) * PAGE_SIZE > cprm->limit)
		dump.u_dsize = 0;

	if ((dump.u_ssize + 1) * PAGE_SIZE > cprm->limit)
		dump.u_ssize = 0;

	set_fs(USER_DS);
	if (!access_ok(VERIFY_READ, START_DATA(dump), dump.u_dsize << PAGE_SHIFT))
		dump.u_dsize = 0;
	if (!access_ok(VERIFY_READ, START_STACK(dump), dump.u_ssize << PAGE_SHIFT))
		dump.u_ssize = 0;

	set_fs(KERNEL_DS);
	if (!dump_write(file, &dump, sizeof(dump)))
		goto end_coredump;
	if (!dump_seek(cprm->file, PAGE_SIZE - sizeof(dump)))
		goto end_coredump;
	set_fs(USER_DS);
	if (dump.u_dsize != 0) {
		dump_start = START_DATA(dump);
		dump_size = dump.u_dsize << PAGE_SHIFT;
		if (!dump_write(file, dump_start, dump_size))
			goto end_coredump;
	}
	if (dump.u_ssize != 0) {
		dump_start = START_STACK(dump);
		dump_size = dump.u_ssize << PAGE_SHIFT;
		if (!dump_write(file, dump_start, dump_size))
			goto end_coredump;
	}
end_coredump:
	set_fs(fs);
	return has_dumped;
}

static unsigned long __user *create_aout_tables(char __user *p, struct linux_binprm * bprm)
{
	char __user * __user *argv;
	char __user * __user *envp;
	unsigned long __user *sp;
	int argc = bprm->argc;
	int envc = bprm->envc;

	sp = (void __user *)((-(unsigned long)sizeof(char *)) & (unsigned long) p);
#ifdef __alpha__
	put_user(0, --sp);
	put_user(0, --sp);
	if (bprm->loader) {
		put_user(0, --sp);
		put_user(1003, --sp);
		put_user(bprm->loader, --sp);
		put_user(1002, --sp);
	}
	put_user(bprm->exec, --sp);
	put_user(1001, --sp);
#endif
	sp -= envc+1;
	envp = (char __user * __user *) sp;
	sp -= argc+1;
	argv = (char __user * __user *) sp;
#ifndef __alpha__
	put_user((unsigned long) envp,--sp);
	put_user((unsigned long) argv,--sp);
#endif
	put_user(argc,--sp);
	current->mm->arg_start = (unsigned long) p;
	while (argc-->0) {
		char c;
		put_user(p,argv++);
		do {
			get_user(c,p++);
		} while (c);
	}
	put_user(NULL,argv);
	current->mm->arg_end = current->mm->env_start = (unsigned long) p;
	while (envc-->0) {
		char c;
		put_user(p,envp++);
		do {
			get_user(c,p++);
		} while (c);
	}
	put_user(NULL,envp);
	current->mm->env_end = (unsigned long) p;
	return sp;
}


static int load_aout_binary(struct linux_binprm * bprm, struct pt_regs * regs)
{
	struct exec ex;
	unsigned long error;
	unsigned long fd_offset;
	unsigned long rlim;
	int retval;

	ex = *((struct exec *) bprm->buf);		
	if ((N_MAGIC(ex) != ZMAGIC && N_MAGIC(ex) != OMAGIC &&
	     N_MAGIC(ex) != QMAGIC && N_MAGIC(ex) != NMAGIC) ||
	    N_TRSIZE(ex) || N_DRSIZE(ex) ||
	    i_size_read(bprm->file->f_path.dentry->d_inode) < ex.a_text+ex.a_data+N_SYMSIZE(ex)+N_TXTOFF(ex)) {
		return -ENOEXEC;
	}

	if (!bprm->file->f_op || !bprm->file->f_op->mmap)
		return -ENOEXEC;

	fd_offset = N_TXTOFF(ex);

	rlim = rlimit(RLIMIT_DATA);
	if (rlim >= RLIM_INFINITY)
		rlim = ~0;
	if (ex.a_data + ex.a_bss > rlim)
		return -ENOMEM;

	
	retval = flush_old_exec(bprm);
	if (retval)
		return retval;

	
#ifdef __alpha__
	SET_AOUT_PERSONALITY(bprm, ex);
#else
	set_personality(PER_LINUX);
#endif
	setup_new_exec(bprm);

	current->mm->end_code = ex.a_text +
		(current->mm->start_code = N_TXTADDR(ex));
	current->mm->end_data = ex.a_data +
		(current->mm->start_data = N_DATADDR(ex));
	current->mm->brk = ex.a_bss +
		(current->mm->start_brk = N_BSSADDR(ex));
	current->mm->free_area_cache = current->mm->mmap_base;
	current->mm->cached_hole_size = 0;

	retval = setup_arg_pages(bprm, STACK_TOP, EXSTACK_DEFAULT);
	if (retval < 0) {
		
		send_sig(SIGKILL, current, 0);
		return retval;
	}

	install_exec_creds(bprm);

	if (N_MAGIC(ex) == OMAGIC) {
		unsigned long text_addr, map_size;
		loff_t pos;

		text_addr = N_TXTADDR(ex);

#ifdef __alpha__
		pos = fd_offset;
		map_size = ex.a_text+ex.a_data + PAGE_SIZE - 1;
#else
		pos = 32;
		map_size = ex.a_text+ex.a_data;
#endif
		error = vm_brk(text_addr & PAGE_MASK, map_size);
		if (error != (text_addr & PAGE_MASK)) {
			send_sig(SIGKILL, current, 0);
			return error;
		}

		error = bprm->file->f_op->read(bprm->file,
			  (char __user *)text_addr,
			  ex.a_text+ex.a_data, &pos);
		if ((signed long)error < 0) {
			send_sig(SIGKILL, current, 0);
			return error;
		}
			 
		flush_icache_range(text_addr, text_addr+ex.a_text+ex.a_data);
	} else {
		if ((ex.a_text & 0xfff || ex.a_data & 0xfff) &&
		    (N_MAGIC(ex) != NMAGIC) && printk_ratelimit())
		{
			printk(KERN_NOTICE "executable not page aligned\n");
		}

		if ((fd_offset & ~PAGE_MASK) != 0 && printk_ratelimit())
		{
			printk(KERN_WARNING 
			       "fd_offset is not page aligned. Please convert program: %s\n",
			       bprm->file->f_path.dentry->d_name.name);
		}

		if (!bprm->file->f_op->mmap||((fd_offset & ~PAGE_MASK) != 0)) {
			loff_t pos = fd_offset;
			vm_brk(N_TXTADDR(ex), ex.a_text+ex.a_data);
			bprm->file->f_op->read(bprm->file,
					(char __user *)N_TXTADDR(ex),
					ex.a_text+ex.a_data, &pos);
			flush_icache_range((unsigned long) N_TXTADDR(ex),
					   (unsigned long) N_TXTADDR(ex) +
					   ex.a_text+ex.a_data);
			goto beyond_if;
		}

		error = vm_mmap(bprm->file, N_TXTADDR(ex), ex.a_text,
			PROT_READ | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_EXECUTABLE,
			fd_offset);

		if (error != N_TXTADDR(ex)) {
			send_sig(SIGKILL, current, 0);
			return error;
		}

		error = vm_mmap(bprm->file, N_DATADDR(ex), ex.a_data,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_EXECUTABLE,
				fd_offset + ex.a_text);
		if (error != N_DATADDR(ex)) {
			send_sig(SIGKILL, current, 0);
			return error;
		}
	}
beyond_if:
	set_binfmt(&aout_format);

	retval = set_brk(current->mm->start_brk, current->mm->brk);
	if (retval < 0) {
		send_sig(SIGKILL, current, 0);
		return retval;
	}

	current->mm->start_stack =
		(unsigned long) create_aout_tables((char __user *) bprm->p, bprm);
#ifdef __alpha__
	regs->gp = ex.a_gpvalue;
#endif
	start_thread(regs, ex.a_entry, current->mm->start_stack);
	return 0;
}

static int load_aout_library(struct file *file)
{
	struct inode * inode;
	unsigned long bss, start_addr, len;
	unsigned long error;
	int retval;
	struct exec ex;

	inode = file->f_path.dentry->d_inode;

	retval = -ENOEXEC;
	error = kernel_read(file, 0, (char *) &ex, sizeof(ex));
	if (error != sizeof(ex))
		goto out;

	
	if ((N_MAGIC(ex) != ZMAGIC && N_MAGIC(ex) != QMAGIC) || N_TRSIZE(ex) ||
	    N_DRSIZE(ex) || ((ex.a_entry & 0xfff) && N_MAGIC(ex) == ZMAGIC) ||
	    i_size_read(inode) < ex.a_text+ex.a_data+N_SYMSIZE(ex)+N_TXTOFF(ex)) {
		goto out;
	}

	if (!file->f_op || !file->f_op->mmap)
		goto out;

	if (N_FLAGS(ex))
		goto out;


	start_addr =  ex.a_entry & 0xfffff000;

	if ((N_TXTOFF(ex) & ~PAGE_MASK) != 0) {
		loff_t pos = N_TXTOFF(ex);

		if (printk_ratelimit())
		{
			printk(KERN_WARNING 
			       "N_TXTOFF is not page aligned. Please convert library: %s\n",
			       file->f_path.dentry->d_name.name);
		}
		vm_brk(start_addr, ex.a_text + ex.a_data + ex.a_bss);
		
		file->f_op->read(file, (char __user *)start_addr,
			ex.a_text + ex.a_data, &pos);
		flush_icache_range((unsigned long) start_addr,
				   (unsigned long) start_addr + ex.a_text + ex.a_data);

		retval = 0;
		goto out;
	}
	
	error = vm_mmap(file, start_addr, ex.a_text + ex.a_data,
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE,
			N_TXTOFF(ex));
	retval = error;
	if (error != start_addr)
		goto out;

	len = PAGE_ALIGN(ex.a_text + ex.a_data);
	bss = ex.a_text + ex.a_data + ex.a_bss;
	if (bss > len) {
		error = vm_brk(start_addr + len, bss - len);
		retval = error;
		if (error != start_addr + len)
			goto out;
	}
	retval = 0;
out:
	return retval;
}

static int __init init_aout_binfmt(void)
{
	register_binfmt(&aout_format);
	return 0;
}

static void __exit exit_aout_binfmt(void)
{
	unregister_binfmt(&aout_format);
}

core_initcall(init_aout_binfmt);
module_exit(exit_aout_binfmt);
MODULE_LICENSE("GPL");
