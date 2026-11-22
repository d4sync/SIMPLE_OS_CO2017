/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "os-mm.h"
#include "syscall.h"
#include "libmem.h"
#include "queue.h"
#include <stdlib.h>

#ifdef MM64
#include "mm64.h"
#else
#include "mm.h"
#endif

//typedef char BYTE;

static struct pcb_t *find_proc_by_pid(struct krnl_t *krnl, uint32_t pid) 
{ 
    //hàm mới viết, lục hết mớ pid trên kernel trả pcb của pid
    //lý do là vì user chỉ có truyền pid nên ta sẽ tìm pcb
   int i;

   if (krnl == NULL || krnl->running_list == NULL)
      return NULL;
   for (i = 0; i < krnl->running_list->size; i++)
   {
      struct pcb_t *p = krnl->running_list->proc[i];
      if (p != NULL && p->pid == pid)
         return p;
   }
   return NULL;
}

int __sys_memmap(struct krnl_t *krnl, uint32_t pid, struct sc_regs* regs)
{
   int memop = regs->a1;
   int ret = 0; // giá trị sẽ trả về từ cái hàm này, lúc đầu mặc định luôn là 0
   BYTE value;
   struct pcb_t *caller; //pcb của tiến trình đang gọi syscall
   /* 
    * TODO THIS DUMMY CREATE EMPTY PROC TO AVOID COMPILER NOTIFY 
    *      need to be eliminated
	
   struct pcb_t *caller = malloc(sizeof(struct pcb_t));
   caller->krnl = malloc(sizeof(struct krnl_t));
    */
   /*
    * @bksysnet: Please note in the dual spacing design
    *            syscall implementations are in kernel space.
    */

   /* TODO: Traverse proclist to terminate the proc
    *       stcmp to check the process match proc_name
    */

    /* TODO Maching and marking the process */
    /* user process are not allowed to access directly pcb in kernel space of syscall */
    //....

    // theo ý hiểu là bây giờ cần tìm pcb real từ pid, lấy pcb đó đưa cho cái bên dưới
    caller = find_proc_by_pid(krnl, pid); // đi tìm
    if (caller == NULL)
        return -1; // không thấy trả -1


   switch (memop) {
   case SYSMEM_MAP_OP:
            /* Reserved process case*/
			vmap_pgd_memset(caller, regs->a2, regs->a3);
            break;
   case SYSMEM_INC_OP:
            ret=inc_vma_limit(caller, regs->a2, regs->a3); // giờ cần tìm hiểu cái này
            break;
   case SYSMEM_SWP_OP:
            __mm_swap_page(caller, regs->a2, regs->a3);
            break;
   case SYSMEM_IO_READ:
            MEMPHY_read(krnl->mram, regs->a2, &value);
            regs->a3 = value;
            break;
   case SYSMEM_IO_WRITE:
            MEMPHY_write(krnl->mram, regs->a2, regs->a3);
            break;
   default:
            printf("Memop code: %d\n", memop);
            ret = -1;
            break;
   }
   
   return ret;
}