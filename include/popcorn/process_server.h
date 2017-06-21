#ifndef __POPCORN_PROCESS_SERVER_H
#define __POPCORN_PROCESS_SERVER_H

struct task_struct;
struct remote_context;

int process_server_do_migration(struct task_struct* tsk, unsigned int dst_nid, void __user *uregs);
int process_server_task_exit(struct task_struct *tsk);

void exit_remote_context(struct remote_context *rc);
#endif /* __POPCORN_PROCESS_SERVER_H */
