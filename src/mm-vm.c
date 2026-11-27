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
    if(!cur || start>=end || 
       end>UINT64_C(0x0100000000000000) || end>p->krnl->kernel_base) return -1;
    for(struct vm_area_struct *v=p->mm->mmap;v;v=v->vm_next)
        if(v!=cur && OVERLAP(start,end,v->vm_start,v->vm_end))return -1;
    return 0;
}

/* Chen theo thu tu dia chi va gop cac khoang lien ke. Node da cap phat
 * truoc khi map RAM, nen khong mat region neu malloc that bai. */


/* Ham goc: tao descriptor tai break, chua thay doi VMA. */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, addr_t alignedsz)
{
    struct vm_area_struct *v=get_vma_by_num(caller->mm,vmaid);
    if(!v||!alignedsz||alignedsz>UINT64_MAX-v->vm_end)return NULL;
    struct vm_rg_struct *r=init_vm_rg(v->vm_end,v->vm_end+alignedsz);
    if(r)r->vmaid=vmaid;
    return r;
}
extern int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt);
int inc_vma_limit(struct pcb_t *p,int id,addr_t size)
{
    struct vm_area_struct *v=get_vma_by_num(p->mm,id);
    if(!v||!size||size>8192ULL*4096)return -1;
    addr_t aligned=(size+4095)&~UINT64_C(4095), start=v->vm_end;
    if(aligned>UINT64_MAX-start || validate_overlap_vm_area(p,id,v->vm_start,start+aligned))return -1;
    struct vm_rg_struct *r=get_vm_area_node_at_brk(p,id,aligned);
    if(!r)return -1;
    struct vm_rg_struct mapped;
    if(vm_map_ram(p,v->vm_start,start+aligned,start,(int)(aligned/4096),&mapped)){free(r);return -1;}
    r->vmaid=id;enlist_vm_freerg_list(p->mm,r);
    v->vm_end=v->sbrk=start+aligned;
    return 0;
}
int __mm_swap_page(struct pcb_t *p, addr_t ram, addr_t swap)
{
    if(!p||!p->krnl->active_mswp)return -1;
    struct framephy_struct *r=p->krnl->mram->used_fp_list,*s=p->krnl->active_mswp->used_fp_list;
    while(r&&r->fpn!=ram)r=r->fp_next;
    while(s&&s->fpn!=swap)s=s->fp_next;
    if(!r||!s||r->owner!=p->mm||s->owner!=p->mm)return -1;
    return __swap_cp_page(p->krnl->mram,ram,p->krnl->active_mswp,swap);
}

