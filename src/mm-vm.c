/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */

#include "mm64.h"
#include <stdlib.h>

struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int id)
{
    if (!mm || id<0) return NULL;
    for (struct vm_area_struct *v=mm->mmap;v;v=v->vm_next)
        if (v->vm_id==(unsigned long)id) return v;
    return NULL;
}

/* Cac khoang la [start,end), hai khoang ke nhau khong overlap. */
int validate_overlap_vm_area(struct pcb_t *p,int id,addr_t start,addr_t end)
{
    struct vm_area_struct *cur=get_vma_by_num(p->mm,id);
    if(!cur || start>=end || !mm_canonical(start) || !mm_canonical(end-1) ||
       end>UINT64_C(0x0100000000000000) || end>p->krnl->kernel_base) return -1;
    for(struct vm_area_struct *v=p->mm->mmap;v;v=v->vm_next)
        if(v!=cur && OVERLAP(start,end,v->vm_start,v->vm_end))return -1;
    return 0;
}

/* Chen theo thu tu dia chi va gop cac khoang lien ke. Node da cap phat
 * truoc khi map RAM, nen khong mat region neu malloc that bai. */
int vm_insert_free(struct vm_area_struct *v,struct vm_rg_struct *r)
{
    if(!v||!r||r->rg_start>=r->rg_end)return -1;
    struct vm_rg_struct **p=&v->vm_freerg_list;
    while(*p && (*p)->rg_start<r->rg_start)p=&(*p)->rg_next;
    r->rg_next=*p;*p=r;
    struct vm_rg_struct *a=v->vm_freerg_list;
    while(a && a->rg_next){struct vm_rg_struct *b=a->rg_next;
        if(a->rg_end>=b->rg_start){if(b->rg_end>a->rg_end)a->rg_end=b->rg_end;a->rg_next=b->rg_next;free(b);}
        else a=b;
    }
    return 0;
}
int inc_vma_limit(struct pcb_t *p,int id,addr_t size)
{
    struct vm_area_struct *v=get_vma_by_num(p->mm,id);
    if(!v||!size||size>8192ULL*4096)return -1;
    addr_t aligned=(size+4095)&~UINT64_C(4095), start=v->vm_end;
    if(aligned>UINT64_MAX-start || validate_overlap_vm_area(p,id,v->vm_start,start+aligned))return -1;
    struct vm_rg_struct *r=init_vm_rg(start,start+aligned);
    if(!r)return -1;
    struct vm_rg_struct mapped;
    if(vm_map_ram(p,v->vm_start,start+aligned,start,(int)(aligned/4096),&mapped)){free(r);return -1;}
    r->vmaid=id;vm_insert_free(v,r);
    v->vm_end=v->sbrk=start+aligned;
    return 0;
}
int __mm_swap_page(struct pcb_t *p,addr_t ram,addr_t swap)
{
    /* Chi ho tro copy frame cho handler noi bo; chua co eviction/page-in. */
    if(!p->krnl->active_mswp || !MEMPHY_owned(p->krnl->mram,ram,p->mm) ||
       !MEMPHY_owned(p->krnl->active_mswp,swap,p->mm))return -1;
    return __swap_cp_page(p->krnl->mram,ram,p->krnl->active_mswp,swap);
}
