/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <errno.h>
#include <limits.h>

int __sys_memmap(struct krnl_t *krnl,uint32_t pid,struct sc_regs *r)
{
    struct pcb_t *p=krnl?krnl->processes:NULL;
    while(p&&p->pid!=pid)p=p->registry_next;
    if(!p)return -ESRCH;
    BYTE b;
    switch(r->a1){
    case SYSMEM_MAP_OP:
        if(r->a3>INT_MAX)return -EINVAL;
        return vmap_pgd_memset(p,r->a2,(int)r->a3);
    case SYSMEM_INC_OP:
        if(r->a2>INT_MAX)return -EINVAL;
        return inc_vma_limit(p,(int)r->a2,r->a3);
    case SYSMEM_SWP_OP:
        if(!(r->flags&SC_INTERNAL))return -EPERM;
        return __mm_swap_page(p,r->a2,r->a3);
    case SYSMEM_IO_READ:
    case SYSMEM_IO_WRITE:
        /* Lenh syscall truc tiep tu user khong duoc doc/ghi raw physical.
         * Wrapper da kiem tra region va translate truoc khi gui IO. */
        if(!(r->flags&SC_INTERNAL))return -EPERM;
        struct framephy_struct *f=krnl->mram->used_fp_list;
        while(f&&f->fpn!=r->a2/4096)f=f->fp_next;
        if(!f||f->owner!=p->mm)return -EPERM;
        if(r->a1==SYSMEM_IO_WRITE)return MEMPHY_write(krnl->mram,r->a2,(BYTE)r->a3);
        if(MEMPHY_read(krnl->mram,r->a2,&b))return -EFAULT;
        r->a3=(unsigned char)b;return 0;
    case SYSMEM_REGION_ALLOC:
    case SYSMEM_REGION_FREE:
    case SYSMEM_REGION_READ:
    case SYSMEM_REGION_WRITE:
    case SYSMEM_KMALLOC:
    case SYSMEM_COPY_FROM:
    case SYSMEM_COPY_TO:
        if(!(r->flags&SC_INTERNAL) || r->a2>=10)return -EPERM;
        switch(r->a1){
        case SYSMEM_REGION_ALLOC:return __alloc(p,0,(int)r->a2,r->a3,&r->a6);
        case SYSMEM_REGION_FREE:return __free(p,0,(int)r->a2);
        case SYSMEM_REGION_READ:
            if(__read(p,0,(int)r->a2,r->a3,&b))return -EFAULT;
            r->a6=(unsigned char)b;return 0;
        case SYSMEM_REGION_WRITE:return __write(p,0,(int)r->a2,r->a3,(BYTE)r->a4);
        case SYSMEM_KMALLOC:return __kmalloc(p,0,(int)r->a2,r->a3,&r->a6)==0 ? 0:-ENOMEM;
        default:
            if(r->a3>=10)return -EINVAL;
            /* Kiem tra ca hai khoang truoc khi copy, khong cat bot du lieu. */
            struct vm_rg_struct *src=get_symrg_byid(p->mm,(int)r->a2),*dst=get_symrg_byid(p->mm,(int)r->a3);
            unsigned smode=r->a1==SYSMEM_COPY_FROM;
            if(!src||!dst||src->rg_end<=src->rg_start||dst->rg_end<=dst->rg_start||src->mode_bit!=smode||dst->mode_bit==smode||src->vmaid!=0||dst->vmaid!=0||r->a4>src->rg_end-src->rg_start||r->a4>dst->rg_end-dst->rg_start||r->a5>src->rg_end-src->rg_start-r->a4||r->a5>dst->rg_end-dst->rg_start-r->a4)return -EINVAL;
            for(addr_t i=0;i<r->a5;++i){
                addr_t off=r->a4+i;BYTE byte;
                int rc=smode?__read_user_mem(p,0,(int)r->a2,off,&byte):__read_kernel_mem(p,0,(int)r->a2,off,&byte);
                if(rc)return rc;
                rc=smode?__write_kernel_mem(p,0,(int)r->a3,off,byte):__write_user_mem(p,0,(int)r->a3,off,byte);
                if(rc)return rc;
            }
            return 0;
        }
    default:return -EINVAL;
    }
}
