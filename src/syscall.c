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
const int syscall_table_size = sizeof(sys_call_table) / sizeof(char*);
/* Registry kernel: PCB chi duoc tim bang PID trong handler.
 * memory_lock bao ve ca lifetime PCB va cac transaction memory. */

/* __sys_ni_syscall — Trả lỗi cho syscall chưa được hỗ trợ.
 * Tham số: k: kernel; regs: thanh ghi syscall (không dùng).
 * Trả về: -ENOSYS.
 */
int __sys_ni_syscall(struct krnl_t *k, struct sc_regs *regs)
{
    (void)k;
    (void)regs;
    return -ENOSYS;
}
#define __SYSCALL(nr, sym) case nr: result=__##sym(krnl,pid,regs);break;
/* _syscall — Cổng vào kernel: khóa, tìm PID và dispatch theo số syscall.
 * Tham số: krnl: kernel; pid: ID process; nr: số syscall; regs: tham số/kết quả.
 * Trả về: Mã handler; -EINVAL nếu regs NULL, -ESRCH nếu PID sai, -ENOSYS nếu syscall lạ.
 */
int _syscall(struct krnl_t *krnl, uint32_t pid, uint32_t nr, struct sc_regs *regs)
{
    if (!regs) {
        return -EINVAL;
    }
    pthread_mutex_lock(&memory_lock);
    int result = -ESRCH;
    // Bước 1: tìm process trong registry theo PID, dưới cùng một khóa.
    struct pcb_t *current_process = NULL;
    if (krnl != NULL) {
        current_process = krnl->processes;
    }
    while (current_process && current_process->pid != pid) {
        current_process = current_process->registry_next;
    }
    if (current_process) {
        regs->orig_ax = nr;
        switch (nr) {
#include "syscalltbl.lst"
        default:
            result = __sys_ni_syscall(krnl, regs);
            break;
        }
    }
    pthread_mutex_unlock(&memory_lock);
    return result;
}
#undef __SYSCALL
