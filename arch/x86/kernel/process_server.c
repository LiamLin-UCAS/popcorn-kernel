/*
 * File:
 * 	process_server.c
 *
 * Description:
 * 	this file implements the x86 architecture specific
 *  helper functionality of the process server
 *
 * Created on:
 * 	Sep 19, 2014
 *
 * Author:
 * 	Sharath Kumar Bhat, SSRG, VirginiaTech
 *
 */

/* File includes */
#include <linux/sched.h>
#include <linux/cpu_namespace.h>
#if 0 // beowulf
#include <asm/i387.h>
#endif
#include <asm/uaccess.h>

#include <popcorn/types.h>
#include <popcorn/debug.h>

/* External function declarations */
extern void __show_regs(struct pt_regs *regs, int all);

/*
 * Function:
 *		save_thread_info
 *
 * Description:
 *		this function saves the architecture specific info of the task
 *		to the field_arch structure passed
 *
 * Input:
 *	task,	pointer to the task structure of the task of which the
 *			architecture specific info needs to be saved
 *
 *	regs,	pointer to the pt_regs field of the task
 *
 * Output:
 *	arch,	pointer to the field_arch structure type where the
 *			architecture specific information of the task has to be
 *			saved
 *
 * Return value:
 *	on success, returns 0
 * 	on failure, returns negative integer
 */
int save_thread_info(struct task_struct *tsk, struct pt_regs *regs,
                     field_arch *arch, void __user *uregs)
{
	unsigned short fsindex, gsindex;
	unsigned long fs, gs;
	unsigned short es, ds;

	//dump_processor_regs(task_pt_regs(tsk));

	BUG_ON(!tsk || !arch);

	if (uregs != NULL) {
		int remain = copy_from_user(&arch->regs_aarch, uregs,
				sizeof(struct popcorn_regset_aarch64));
		BUG_ON(remain != 0);
	}

	memcpy(&arch->regs, regs, sizeof(struct pt_regs));

	/*
	 * Save frame pointer and return address, required for stack
	 * transformation.
	 */
	arch->migration_pc = tsk->migration_pc;
	arch->ip = regs->ip;
	arch->ra = tsk->return_addr;
	arch->bp = regs->bp;
	arch->sp = regs->sp;

	/* Segments */
	arch->thread_ds = tsk->thread.ds;
	savesegment(ds, ds);

	arch->thread_es = tsk->thread.es;
	savesegment(es, es);

	arch->thread_fsindex = tsk->thread.fsindex;
	savesegment(fs, fsindex);
	if (fsindex != arch->thread_fsindex) {
		arch->thread_fsindex = fsindex;
	}

	arch->thread_fs = tsk->thread.fs;
	rdmsrl(MSR_FS_BASE, fs);
	if (fs != arch->thread_fs) {
		arch->thread_fs = fs;
	}

	arch->thread_gsindex = tsk->thread.gsindex;
	savesegment(gs, gsindex);
	if (gsindex != arch->thread_gsindex) {
		arch->thread_gsindex = gsindex;
	}

	arch->thread_gs = tsk->thread.gs;
	rdmsrl(MSR_KERNEL_GS_BASE, gs);
	if (gs != arch->thread_gs) {
		arch->thread_gs = gs;
	}

#ifdef MIGRATE_FPU
	save_fpu_info(tsk, arch);
#endif

	PSPRINTK(KERN_INFO"%s: ip 0x%lx pc 0x%lx\n", __func__,
			arch->ip, arch->migration_pc);
	PSPRINTK(KERN_INFO"%s: sp 0x%lx bp 0x%lx\n", __func__,
			arch->sp, arch->bp);
	PSPRINTK(KERN_INFO"%s: ra 0x%lx\n", __func__, arch->ra);

	PSPRINTK(KERN_INFO"%s: fs task 0x%lx[0x%lx]\n", __func__,
		(unsigned long)tsk->thread.fs, (unsigned long)tsk->thread.fsindex);
	PSPRINTK(KERN_INFO"%s:   saved 0x%lx[0x%lx]\n", __func__,
		(unsigned long)arch->thread_fs, (unsigned long)arch->thread_fsindex);
	PSPRINTK(KERN_INFO"%s: current 0x%lx[0x%lx]\n", __func__,
		(unsigned long)fs, (unsigned long)fsindex);

	return 0;
}

/*
 * Function:
 *		restore_thread_info
 *
 * Description:
 *		this function restores the architecture specific info of the
 *		task from the field_arch structure passed
 *
 * Input:
 * 	tsk,	pointer to the task structure of the task of which the
 * 			architecture specific info needs to be restored
 *
 * 	arch,	pointer to the field_arch structure type from which the
 *			architecture specific information of the task has to be
 *			restored
 *
 * Output:
 *	none
 *
 * Return value:
 *	on success, returns 0
 * 	on failure, returns negative integer
 */
int restore_thread_info(struct task_struct *tsk, field_arch *arch)
{
	struct pt_regs *regs = task_pt_regs(tsk);

	memcpy(regs, &arch->regs, sizeof(struct pt_regs));

	regs->ip = arch->migration_pc;
	tsk->return_addr = arch->ra;
	regs->bp = arch->bp;
	regs->sp = arch->sp;

	regs->cs = __USER_CS;
	regs->ss = __USER_DS;

	tsk->thread.ds = arch->thread_ds;
	tsk->thread.es = arch->thread_es;
	tsk->thread.fsindex = arch->thread_fsindex;
	tsk->thread.fs = arch->thread_fs;
	tsk->thread.gsindex = arch->thread_gsindex;
	tsk->thread.gs = arch->thread_gs;

#ifdef MIGRATE_FPU
	restore_fpu_info(tsk, arch);
#endif
	/*
	unsigned long fsindex, fs;
	rdmsrl(MSR_FS_BASE, fs);
	savesegment(fs, fsindex);
	PSPRINTK(KERN_INFO"%s:  current 0x%lx[0x%lx]\n", __func__,
		(unsigned long)fs, (unsigned long)fsindex);
	*/

	PSPRINTK(KERN_INFO"%s: ip 0x%lx pc 0x%lx\n", __func__,
			arch->ip, arch->migration_pc);
	PSPRINTK(KERN_INFO"%s: sp 0x%lx bp 0x%lx\n", __func__,
			arch->sp, arch->bp);
	PSPRINTK(KERN_INFO"%s: ra 0x%lx\n", __func__, arch->ra);
	PSPRINTK(KERN_INFO"%s: fs saved 0x%lx[0x%lx]\n", __func__,
		(unsigned long)arch->thread_fs, (unsigned long)arch->thread_fsindex);

	return 0;
}

#if 0
int restore_thread_info_from_aarch64(struct task_struct *task, field_arch *arch)
{
	int passed;
	unsigned long fsindex, fs_val;

	BUG_ON(!task || !arch);

	/* For het migration */
	if (arch->migration_pc != 0) {
		struct pt_regs *pt_regs = task_pt_regs(tsk);

		pt_regs->ip = arch->migration_pc;
		pt_regs->bp = arch->bp;
		/* pt_regs->sp = tsk->saved_old_rsp; */
		pt_regs->sp = arch->old_rsp;
		tsk->thread.usersp = arch->old_rsp;

		pt_regs->cs = __USER_CS;
		pt_regs->ss = __USER_DS;
		/*
		pt_regs->cs = __KERNEL_CS | get_kernel_rpl();
		pt_regs->ds = __USER_DS;
		printk("%s: cs=0x%x, KERNEL_CS=0x%lx\n", __func__,
				pt_regs->cs, __KERNEL_CS);
		*/

		pt_regs->r15 = arch->regs_x86.r15;
		pt_regs->r14 = arch->regs_x86.r14;
		pt_regs->r13 = arch->regs_x86.r13;
		pt_regs->r12 = arch->regs_x86.r12;
		pt_regs->r11 = arch->regs_x86.r11;
		pt_regs->r10 = arch->regs_x86.r10;
		pt_regs->r9 = arch->regs_x86.r9;
		pt_regs->r8 = arch->regs_x86.r8;
		pt_regs->ax = arch->regs_x86.rax;
		pt_regs->dx = arch->regs_x86.rdx;
		pt_regs->cx = arch->regs_x86.rcx;
		pt_regs->bx = arch->regs_x86.rbx;
		pt_regs->si = arch->regs_x86.rsi;
		pt_regs->di = arch->regs_x86.rdi;
	}

	tsk->thread.fs = arch->thread_fs;
	tsk->thread.fsindex = arch->thread_fsindex;

	passed = 1;
	if (current == task) {
		loadsegment(fs, arch->thread_fsindex);
		wrmsrl(MSR_FS_BASE, arch->thread_fs);
		passed = 2;
	}

#ifdef MIGRATE_FPU
	restore_fpu_info(task, arch);
#endif

	savesegment(fs, fsindex);
	rdmsrl(MSR_FS_BASE, fs_val);

	PSPRINTK(KERN_INFO"%s: ip 0x%lx, sp 0x%lx\n", __func__,
			arch->migration_pc, arch->old_rsp);
	PSPRINTK(KERN_INFO"%s: bp 0x%lx\n", __func__, arch->bp);

	PSPRINTK(KERN_INFO"%s: fs saved 0x%lx[0x%lx], "
			"curr 0x%lx[0x%lx]\n", __func__,
			(unsigned long)arch->thread_fs, (unsigned long)arch->thread_fsindex,
			(unsigned long)fs_val, (unsigned long)fsindex);

	//dump_processor_regs(task_pt_regs(tsk));

	return 0;
}
#endif

/*
 * Function:
 *		update_thread_info
 *
 * Description:
 *		this function updates the task's thread structure to
 *		the latest register values.
 *
 * Input:
 * 	task,	pointer to the task structure of the task of which the
 * 			thread structure needs to be updated
 *
 * Output:
 * 	none
 *
 * Return value:
 *	on success, returns 0
 * 	on failure, returns negative integer
 */
int update_thread_info(void)
{
	unsigned int fsindex, gsindex;

	//printk("%s [+] TID: %d\n", __func__, current->pid);
	BUG_ON(!current);

	savesegment(fs, fsindex);
	if (unlikely(fsindex | current->thread.fsindex))
		loadsegment(fs, current->thread.fsindex);
	else
		loadsegment(fs, 0);

	if (current->thread.fs)
		wrmsrl_safe(MSR_FS_BASE, current->thread.fs);

	savesegment(gs, gsindex);
	if (unlikely(gsindex | current->thread.gsindex))
		load_gs_index(current->thread.gsindex);
	else
		load_gs_index(0);

	if (current->thread.gs)
		wrmsrl_safe(MSR_KERNEL_GS_BASE, current->thread.gs);

#ifdef MIGRATE_FPU
	update_fpu_info(current);
#endif

	//dump_processor_regs(task_pt_regs(tsk));
	//__show_regs(task_pt_regs(tsk), 1);

	//printk("%s [-] TID: %d\n", __func__, tsk->pid);
	return 0;
}

/*
 * Function:
 *		initialize_thread_retval
 *
 * Description:
 *		this function sets the return value of the task
 *		to the value specified in the argument val
 *
 * Input:
 * 	task,	pointer to the task structure of the task of which the
 * 			return value needs to be set
 * 	val,	the return value to be set
 *
 * Output:
 * 	none
 *
 * Return value:
 *	on success, returns 0
 * 	on failure, returns negative integer
 */
int initialize_thread_retval(struct task_struct *tsk, int val)
{
	//printk("%s [+] TID: %d\n", __func__, tsk->pid);
	if (tsk == NULL) {
		printk(KERN_ERR"process_server: invalid params to %s", __func__);
		return -EINVAL;
	}
	task_pt_regs(tsk)->ax = val;
	//printk("%s [-] TID: %d\n", __func__, tsk->pid);

	return 0;
}


#ifdef MIGRATE_FPU
/*/
 * Function:
 *		save_fpu_info
 *
 * Description:
 *		this function saves the FPU info of the task specified
 *		to the arch structure specified in the argument
 *
 * Input:
 * 	task,	pointer to the task structure of the task of which the
 * 			FPU info needs to be saved
 *
 * Output:
 * 	arch,	pointer to the field_arch structure where the FPU info
 * 			needs to be saved
 *
 * Return value:
 *	on success, returns 0
 * 	on failure, returns negative integer
 */
int save_fpu_info(struct task_struct *tsk, field_arch *arch)
{
	struct fpu temp;

	//printk("%s [+] TID: %d\n", __func__, tsk->pid);
	if ((tsk == NULL)  || (arch == NULL)) {
		printk(KERN_ERR"process_server: invalid params to %s", __func__);
		return -EINVAL;
	}

	//FPU migration code --- initiator
	//PSPRINTK(KERN_ERR "%s: task flags %x fpu_counter %x has_fpu %x [%d:%d] %d:%d %x\n",
	//		__func__, tsk->flags, (int)tsk->fpu_counter, (int)tsk->thread.has_fpu,
	//		(int)__thread_has_fpu(tsk), (int)fpu_allocated(&tsk->thread.fpu),
	//		(int)use_xsave(), (int)use_fxsr(), (int) PF_USED_MATH);

	arch->task_flags = tsk->flags;
	arch->task_fpu_counter = tsk->thread.fpu.counter;
	arch->thread_has_fpu = tsk->thread.fpu.fpregs_active;

	//    if (__thread_has_fpu(tsk)) {
	if (!fpu_allocated(&tsk->thread.fpu)){
		fpu_alloc(&tsk->thread.fpu);
		fpu_finit(&tsk->thread.fpu);
	}

	fpu_save_init(&tsk->thread.fpu);

	temp.state = &request->fpu_state;
	fpu_copy(&temp,&tsk->thread.fpu);
	//printk("%s [-] TID: %d\n", __func__, tsk->pid);

	return 0;
}

/*
 * Function:
 *		restore_fpu_info
 *
 * Description:
 *		this function restores the FPU info of the task specified
 *		from the arch structure specified in the argument
 *
 * Input:
 * 	task,	pointer to the task structure of the task of which the
 * 			FPU info needs to be restored
 *
 * 	arch,	pointer to the field_arch struture from where the fpu info
 * 			needs to be restored
 *
 * Output:
 * 	none
 *
 * Return value:
 *	on success, returns 0
 * 	on failure, returns negative integer
 */
int restore_fpu_info(struct task_struct *tsk, field_arch *arch)
{
	struct fpu fpu;
	//printk("%s [+] TID: %d\n", __func__, tsk->pid);
	BUG_ON(!tsk || !arch);

	//FPU migration code --- server
	/* PF_USED_MATH is set if the task used the FPU before
	 * fpu_counter is incremented every time you go in __switch_to while owning the FPU
	 * has_fpu is true if the task is the owner of the FPU, thus the FPU contains its data
	 * fpu.preload (see arch/x86/include/asm.i387.h:switch_fpu_prepare()) is a heuristic
	 */
	if (arch->task_flags & PF_USED_MATH)
	//set_used_math();
	set_stopped_child_used_math(tsk);

	tsk->fpu_counter = arch->task_fpu_counter;

	if (!fpu_allocated(&tsk->thread.fpu)) {
		fpu_alloc(&tsk->thread.fpu);
		fpu_finit(&tsk->thread.fpu);
	}

	fpu.state = &arch->fpu_state;
	fpu_copy(&tsk->thread.fpu, &fpu);

	//PSPRINTK(KERN_ERR "%s: task flags %x fpu_counter %x has_fpu %x [%d:%d]\n",
	//		__func__, tsk->flags, (int)tsk->fpu_counter, (int)tsk->thread.has_fpu,
	//		(int)__thread_has_fpu(tsk), (int)fpu_allocated(&tsk->thread.fpu));

	//FPU migration code --- is the following optional?
	if (tsk_used_math(tsk) && tsk->fpu_counter >5)//fpu.preload
	__math_state_restore(tsk);

	//printk("%s [-] TID: %d\n", __func__, tsk->pid);
	return 0;
}

/*
 * Function:
 *		update_fpu_info
 *
 * Description:
 *		this function updates the FPU info of the task specified
 *
 * Input:
 * 	task,	pointer to the task structure of the task of which the
 * 			FPU info needs to be updated
 *
 * Output:
 * 	none
 *
 * Return value:
 *	on success, returns 0
 * 	on failure, returns negative integer
 */
int update_fpu_info(struct task_struct *tsk)
{
	//printk("%s [+] TID: %d\n", __func__, tsk->pid);
	if (tsk == NULL){
		printk(KERN_ERR"process_server: invalid params to %s", __func__);
		return -EINVAL;
	}

	if (tsk_used_math(tsk) && tsk->fpu_counter >5) //fpu.preload
		__math_state_restore(tsk);
	//printk("%s [-] TID: %d\n", __func__, tsk->pid);

	return 0;
}

#endif	// MIGRATE_FPU


/*
 * Function:
 *		dump_processor_regs
 *
 * Description:
 *		this function prints the architecture specific registers specified
 *		in the input argument
 *
 * Input:
 * 	task,	pointer to the architecture specific registers
 *
 * Output:
 * 	none
 *
 * Return value:
 *	void
 */
void dump_processor_regs(struct pt_regs* regs)
{
	unsigned long fs, gs;
	unsigned long fsindex, gsindex;

	dump_stack();
	if (!regs) return;
	printk(KERN_ALERT"DUMP REGS %s\n", __func__);

	if (NULL != regs) {
		printk(KERN_ALERT"r15{%lx}\n",regs->r15);
		printk(KERN_ALERT"r14{%lx}\n",regs->r14);
		printk(KERN_ALERT"r13{%lx}\n",regs->r13);
		printk(KERN_ALERT"r12{%lx}\n",regs->r12);
		printk(KERN_ALERT"r11{%lx}\n",regs->r11);
		printk(KERN_ALERT"r10{%lx}\n",regs->r10);
		printk(KERN_ALERT"r9{%lx}\n",regs->r9);
		printk(KERN_ALERT"r8{%lx}\n",regs->r8);
		printk(KERN_ALERT"bp{%lx}\n",regs->bp);
		printk(KERN_ALERT"bx{%lx}\n",regs->bx);
		printk(KERN_ALERT"ax{%lx}\n",regs->ax);
		printk(KERN_ALERT"cx{%lx}\n",regs->cx);
		printk(KERN_ALERT"dx{%lx}\n",regs->dx);
		printk(KERN_ALERT"di{%lx}\n",regs->di);
		printk(KERN_ALERT"orig_ax{%lx}\n",regs->orig_ax);
		printk(KERN_ALERT"ip{%lx}\n",regs->ip);
		printk(KERN_ALERT"cs{%lx}\n",regs->cs);
		printk(KERN_ALERT"flags{%lx}\n",regs->flags);
		printk(KERN_ALERT"sp{%lx}\n",regs->sp);
		printk(KERN_ALERT"ss{%lx}\n",regs->ss);
	}
	rdmsrl(MSR_FS_BASE, fs);
	rdmsrl(MSR_GS_BASE, gs);
	printk(KERN_ALERT"fs{%lx} - %lx content %lx\n",fs, current->thread.fs, fs ? * (unsigned long*)fs : 0x1234567l);
	printk(KERN_ALERT"gs{%lx} - %lx content %lx\n",gs, current->thread.gs, fs ? * (unsigned long*)gs : 0x1234567l);

	savesegment(fs, fsindex);
	savesegment(gs, gsindex);
	printk(KERN_ALERT"fsindex{%lx} - %x\n",fsindex, current->thread.fsindex);
	printk(KERN_ALERT"gsindex{%lx} - %x\n",gsindex, current->thread.gsindex);
	printk(KERN_ALERT"REGS DUMP COMPLETE\n");
}

#if 0 // beowulf. removed
void suggest_migration(int suggestion)
{
	vpopcorn_migrate = suggestion;
}
#endif
