#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <asm/ptrace.h>
#include "syscall_redirect.h"
#include "wait_station.h"
#include "types.h"

typedef long (*syscall_fn_t)(const struct pt_regs *regs);
extern const syscall_fn_t sys_call_table[];
const int redirect_table[] = {__NR_socket ,__NR_clock_gettime ,__NR_setsockopt , __NR_bind ,
			      __NR_listen , __NR_accept4 , __NR_shutdown,
			      __NR_recvfrom, __NR_epoll_create1 , -1/*__NR_epoll_waiti*/, 
			      __NR_epoll_pwait , __NR_epoll_ctl , __NR_read ,
			      __NR_write , -1/* __NR_open*/ , __NR_close , __NR_ioctl,
                              __NR_writev , __NR_fstat , __NR_sendfile , -1 /*__NR_select*/,
                	      __NR_fcntl , -1 /* __NR_stat*/ , __NR_getpid , __NR_getuid,
			      __NR_dup, __NR_openat , __NR_gettimeofday , __NR_statx ,
			      __NR_pselect6  } ;

static inline void syscall_get_arg(struct task_struct *task,
                                         struct pt_regs *regs,
                                         unsigned long *args)
{
        args[0] = regs->orig_x0;
        args++;

        memcpy(args, &regs->regs[1], 5 * sizeof(args[0]));
}

static inline void syscall_set_arg(struct task_struct *task,
                                         struct pt_regs *regs,
                                         const unsigned long *args)
{
        regs->orig_x0 = args[0];
        args++;

        memcpy(&regs->regs[1], args, 5 * sizeof(args[0]));
}
/*
 * Handling the signal sent from origin node to remote node
 * We manually force the signal in the destination PID
 */
int handle_signal_remotes(struct pcn_kmsg_message  *msg)
{
       signal_trans_t * recv = (signal_trans_t*)msg;
       struct task_struct * tgt_tsk = find_task_by_vpid(recv->remote_pid);
       printk(KERN_INFO"received the signal %d for task %d \n\n",
                       recv->sig,recv->remote_pid);
       force_sig(recv->sig, tgt_tsk);
       tgt_tsk->remote->stop_remote_worker = false;
       return 0;
}
EXPORT_SYMBOL(handle_signal_remotes);

/*
 * A signal arrived at the origin node for a process that is currently
 * migrated.We are sending the request to remote node that the process is
 * currently stationed.
 */
int remote_signalling(int sig ,struct task_struct * tsk , int group )
{
       int re;
       signal_trans_t *sigreq = pcn_kmsg_get(sizeof(*sigreq));
       sigreq->origin_pid = tsk->pid;
       sigreq->remote_pid = tsk->remote_pid;
       sigreq->remote_nid = tsk->remote_nid;
       sigreq->sig        = sig;
       sigreq->group      = group ? 1:0;
       re = pcn_kmsg_post(PCN_KMSG_TYPE_SIGNAL_FWD,
                       tsk->remote_nid, sigreq, sizeof(*sigreq));
       return 0;
}
EXPORT_SYMBOL(remote_signalling);
long syscall_redirect(unsigned long nr, struct pt_regs *regs)	
{									
	int ret = 0 ,i = 0;	
	syscall_fwd_t *req = pcn_kmsg_get(sizeof(syscall_fwd_t));
	syscall_rep_t *rep = NULL;					
	struct wait_station *ws = get_wait_station(current);		
	
	req->origin_pid = current->origin_pid;				
	req->remote_ws = ws->id;					
	req->call_type    = -1;
	syscall_get_arg(current,regs,(unsigned long *)&req->args);
	printk("Parameters are %x \n%x \n%x \n%x \n%x \n%x\n",req->args[0],req->args[1],req->args[2],req->args[3],req->args[4],req->args[5]);
	while(i <= PCN_NUM_SYSCALLS)
	{
		if(redirect_table[i] == nr)
		{
			req->call_type = i;
			break;
		}
		i++;
	}
	printk(KERN_INFO "redirect called for #syscall %d at index %d \n",nr,i);		
	if(req->call_type  == -1 ){
		BUG_ON( req->call_type == -1  );
		printk(KERN_INFO "redirect called for #syscall %d  \
			not yet implemented",nr);
		return -1;
	}
	else{
		ret = pcn_kmsg_post(PCN_KMSG_TYPE_SYSCALL_FWD, 0, 
				req,sizeof(*req));				
		rep = wait_at_station(ws);					
		ret = rep->ret;							
		pcn_kmsg_done(rep);						

		printk(KERN_INFO "redirect called for #syscall %d with return value %d",nr,ret);
	}
	return ret;							
}
int process_remote_syscall(struct pcn_kmsg_message *msg)
{
	int retval,temp;
	int (* remote_syscall)(struct pt_regs * ) ;
	syscall_fwd_t *req = (syscall_fwd_t *) msg;
	syscall_rep_t *rep = pcn_kmsg_get(sizeof(*rep));
	struct pt_regs reg; 
	printk(KERN_INFO "remote system call num %d received\n\n "  \
		,redirect_table[req->call_type]);
	syscall_set_arg(current,&reg,(unsigned long *)&req->args);	
	
//	do_syscall_64(redirect_table[req->call_type] ,&reg);	
	const syscall_fn_t syscallfn =   
		sys_call_table[redirect_table[req->call_type]];
	retval = syscallfn(&reg);	

	rep->origin_pid = current->origin_pid;
	rep->remote_ws = req->remote_ws;
	rep->ret = retval;
	pcn_kmsg_post(PCN_KMSG_TYPE_SYSCALL_REP, 
		current->remote_nid, rep,sizeof(*rep));
	pcn_kmsg_done(req);
	printk(KERN_INFO"Return value from master %d\n\n for syscall \
		number %d ",retval,redirect_table[req->call_type]);
	return retval;

		
}

static int handle_syscall_reply(struct pcn_kmsg_message *msg)
{
         syscall_rep_t *rep = (syscall_rep_t *)msg;
         struct wait_station *ws = wait_station(rep->remote_ws);

         ws->private = rep;
         complete(&ws->pendings);
         return 0;
}

DEFINE_KMSG_RW_HANDLER(syscall_fwd, syscall_fwd_t, origin_pid);

int __init syscall_server_init(void)
{
	REGISTER_KMSG_HANDLER(PCN_KMSG_TYPE_SYSCALL_FWD,
			      syscall_fwd);
	REGISTER_KMSG_HANDLER(PCN_KMSG_TYPE_SYSCALL_REP,
			      syscall_reply);
	REGISTER_KMSG_HANDLER(PCN_KMSG_TYPE_SIGNAL_FWD,
                              signal_remotes);
	return 0;
}
