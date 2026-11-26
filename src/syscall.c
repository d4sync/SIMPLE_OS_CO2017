/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "syscall.h"
#include "common.h"
#include "mm64.h"
#include <errno.h>

#define __SYSCALL(nr, sym) extern int __##sym(struct krnl_t*, uint32_t,struct sc_regs*);
#include "syscalltbl.lst"
#undef  __SYSCALL

/*
 * The sys_call_table[] is used for system calls, but to know the system
 * call address.
 */
#define __SYSCALL(nr, sym) #nr "-" #sym,
const char* sys_call_table[] = {
#include "syscalltbl.lst"
};
#undef  __SYSCALL
const int syscall_table_size = sizeof(sys_call_table)/sizeof(char*);

/* Registry kernel: PCB chi duoc tim bang PID trong handler.
 * mm_lock bao ve ca lifetime PCB va cac transaction memory. */
struct pcb_t *mm_lookup(struct krnl_t *k, uint32_t pid)
{
    if(!k || !pid)return NULL;
    for(struct pcb_t *p=k->processes;p;p=p->registry_next)
        if(p->pid==pid)return p;
    return NULL;
}
int mm_register(struct pcb_t *p)
{
    if(!p||!p->krnl||!p->pid)return -1;
    mm_lock();if(mm_lookup(p->krnl,p->pid)){mm_unlock();return -1;}
    p->registry_next=p->krnl->processes;p->krnl->processes=p;mm_unlock();return 0;
}
void mm_unregister(struct pcb_t *p)
{
    mm_lock();struct pcb_t **q=&p->krnl->processes;
    while(*q&&*q!=p)q=&(*q)->registry_next;
    if(*q)*q=p->registry_next;
    p->registry_next=NULL;mm_unlock();
}
int __sys_ni_syscall(struct krnl_t *k,struct sc_regs *r)
{
    (void)k;(void)r;return -ENOSYS;
}
#define __SYSCALL(nr, sym) case nr: rc=__##sym(krnl,pid,regs);break;
int _syscall(struct krnl_t *krnl,uint32_t pid,uint32_t nr,struct sc_regs *regs)
{
    if(!regs)return -EINVAL;
    mm_lock();int rc=-ESRCH;
    if(mm_lookup(krnl,pid)){
        regs->orig_ax=nr;
        switch(nr){
        #include "syscalltbl.lst"
        default:rc=__sys_ni_syscall(krnl,regs);break;
        }
    }
    mm_unlock();return rc;
}
#undef __SYSCALL
