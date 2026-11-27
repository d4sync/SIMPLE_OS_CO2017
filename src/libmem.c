/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * System Library
 * Memory Module Library libmem.c
 */

#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <limits.h>

#define REG_COUNT 10

struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm,int id)
{
    return mm && id>=0 && id<REG_COUNT ? &mm->symrgtbl[id]:NULL;
}
int enlist_vm_freerg_list(struct mm_struct *mm,struct vm_rg_struct *r)
{
    struct vm_area_struct *v=r?get_vma_by_num(mm,r->vmaid):NULL;
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

int get_free_vmrg_area(struct pcb_t *p,int id,int size,struct vm_rg_struct *r)
{
    struct vm_area_struct *v=get_vma_by_num(p->mm,id);
    if(!v||size<=0||!r)return -1;
    struct vm_rg_struct **q=&v->vm_freerg_list;
    for(;*q;q=&(*q)->rg_next){struct vm_rg_struct *f=*q;
        if((addr_t)size>f->rg_end-f->rg_start)continue;
        *r=(struct vm_rg_struct){.vmaid=id,.mode_bit=1,.rg_start=f->rg_start,.rg_end=f->rg_start+size};
        f->rg_start+=size;
        if(f->rg_start==f->rg_end){*q=f->rg_next;free(f);}
        return 0;
    }
    return -1;
}

/* Libmem la wrapper; tham so qua syscall chi la PID va thanh ghi. */

int __alloc(struct pcb_t *p,int id,int reg,addr_t size,addr_t *out)
{
    int rc=-1;pthread_mutex_lock(&memory_lock);
    struct vm_rg_struct *s=get_symrg_byid(p->mm,reg),r;
    struct vm_area_struct *v=get_vma_by_num(p->mm,id);
    if(!s||(s && s->rg_end>s->rg_start)||!v||!out||!size||size>INT_MAX)goto done;
    if(get_free_vmrg_area(p,id,(int)size,&r)){
        /* Neu tail da co mot phan free, chi xin them phan thieu. */
        addr_t missing=size;
        for(struct vm_rg_struct *f=v->vm_freerg_list;f;f=f->rg_next)
            if(f->rg_end==v->vm_end && f->rg_end-f->rg_start<size)missing-=f->rg_end-f->rg_start;
        struct sc_regs req={.a1=SYSMEM_INC_OP,.a2=id,.a3=missing,.flags=SC_INTERNAL};
        if(_syscall(p->krnl,p->pid,17,&req)||get_free_vmrg_area(p,id,(int)size,&r))goto done;
    }
    /* Xoa data cu khi tai su dung free region, tranh ro ri noi dung. */
    for(addr_t a=r.rg_start;a<r.rg_end;){addr_t pa;
        addr_t e=pte_get_entry(p->mm,a>>12,p);
        if(!PAGING_PAGE_PRESENT(e)||(e&PAGING_PTE_SWAPPED_MASK))goto done;
        pa=PAGING_FPN(e)*4096+(a&4095);++p->mm->walks;p->mm->pt_accesses+=5;
        addr_t n=4096-(a&4095);if(n>r.rg_end-a)n=r.rg_end-a;
        memset(p->krnl->mram->storage+pa,0,n);a+=n;p->mm->data_accesses+=n;
    }
    *s=r;*out=r.rg_start;p->regs[reg]=r.rg_start;rc=0;
#ifdef IODUMP
    printf("[REGION] PID=%u reg=%d mode=user range=[%016"PRIx64",%016"PRIx64") sbrk=%016"PRIx64"\n",p->pid,reg,r.rg_start,r.rg_end,v->sbrk);
    print_list_rg(v->vm_freerg_list);
#endif

done:pthread_mutex_unlock(&memory_lock);return rc;
}

/* FREE user tra region ve free list; frame giu lai de tai su dung.
 * FREE kernel bo mapping va tra ca day frame ngay. */
int __free(struct pcb_t *p,int id,int reg)
{
    int rc=-1;pthread_mutex_lock(&memory_lock);struct vm_rg_struct *s=get_symrg_byid(p->mm,reg);
    if(!(s && s->rg_end>s->rg_start)||s->vmaid!=id)goto done;
    if(s->mode_bit){struct vm_rg_struct *r=malloc(sizeof(*r));if(!r)goto done;*r=*s;r->rg_next=NULL;
        if(enlist_vm_freerg_list(p->mm,r)){free(r);goto done;}
    }else{
        addr_t n=(s->rg_end-s->rg_start+4095)/4096;
        for(addr_t i=0;i<n;++i){addr_t pg=(s->rg_start>>12)+i,e=pte_get_entry(p->krnl->mm,pg,p);
            if(PAGING_PAGE_PRESENT(e))MEMPHY_put_freefp(p->krnl->mram,PAGING_FPN(e));
            pte_set_entry(p->krnl->mm,pg,0,p);}
    }
    #ifdef IODUMP
    printf("[FREE] PID=%u reg=%d mode=%lu range=[%016"PRIx64",%016"PRIx64")\n",p->pid,reg,s->mode_bit,s->rg_start,s->rg_end);
#endif
    memset(s,0,sizeof(*s));p->regs[reg]=0;rc=0;
#ifdef IODUMP
    print_list_rg(p->mm->mmap->vm_freerg_list);
#endif

done:pthread_mutex_unlock(&memory_lock);return rc;
}

/* Moi byte: walk 5 cap, kiem tra mode/owner, sau do truy cap RAM.
 * User IO phai qua syscall; kernel IO khong lo dia chi vat ly ra user. */

/* Prototype int cua skeleton: chi nhan dia chi khong am. */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
    if(pgn<0||!fpn)return -1;
    addr_t e=pte_get_entry(mm,(addr_t)pgn,caller);
    if(!PAGING_PAGE_PRESENT(e)||(e&PAGING_PTE_SWAPPED_MASK))return -1;
    *fpn=(int)PAGING_FPN(e);return 0;
}
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
    int fpn; if(addr<0||!data||pg_getpage(mm,addr>>12,&fpn,caller))return -1;
    struct framephy_struct *f=caller->krnl->mram->used_fp_list;
    while(f&&f->fpn!=(addr_t)fpn)f=f->fp_next;
    if(!f||f->owner!=mm)return -1;
    ++mm->walks;mm->pt_accesses+=5;
    int rc=MEMPHY_read(caller->krnl->mram,(addr_t)fpn*4096+(addr&4095),data);if(!rc)++mm->data_accesses;return rc;
}
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
    int fpn; if(addr<0||pg_getpage(mm,addr>>12,&fpn,caller))return -1;
    struct framephy_struct *f=caller->krnl->mram->used_fp_list;
    while(f&&f->fpn!=(addr_t)fpn)f=f->fp_next;
    if(!f||f->owner!=mm)return -1;
    ++mm->walks;mm->pt_accesses+=5;
    int rc=MEMPHY_write(caller->krnl->mram,(addr_t)fpn*4096+(addr&4095),value);
    if(!rc){++mm->data_accesses;addr_t e=pte_get_entry(mm,(addr_t)addr>>12,caller);pte_set_entry(mm,(addr_t)addr>>12,e|PAGING_PTE_DIRTY_MASK,caller);}return rc;
}
int __read(struct pcb_t *p,int id,int reg,addr_t off,BYTE *b)
{
    pthread_mutex_lock(&memory_lock);int rc=-1;
    struct vm_rg_struct *r=get_symrg_byid(p->mm,reg);
    if(!r||r->rg_end<=r->rg_start||r->vmaid!=id||r->mode_bit!=1||off>=r->rg_end-r->rg_start||!b)goto done;
    struct mm_struct *mm=p->mm;
    addr_t va=r->rg_start+off,e=pte_get_entry(mm,va>>12,p);
    if(!PAGING_PAGE_PRESENT(e)||(e&PAGING_PTE_SWAPPED_MASK))goto done;
    struct framephy_struct *fp=p->krnl->mram->used_fp_list;
    while(fp&&fp->fpn!=PAGING_FPN(e))fp=fp->fp_next;
    if(!fp||fp->owner!=mm)goto done;
    addr_t pa=PAGING_FPN(e)*4096+(va&4095);
    ++mm->walks;mm->pt_accesses+=5;
    struct sc_regs req={.a1=SYSMEM_IO_READ,.a2=pa,.a3=0,.flags=SC_INTERNAL};
    rc=_syscall(p->krnl,p->pid,17,&req);if(!rc)*b=(BYTE)req.a3;
    if(!rc){++mm->data_accesses;}
done:pthread_mutex_unlock(&memory_lock);return rc;
}

int __write(struct pcb_t *p,int id,int reg,addr_t off,BYTE b)
{
    pthread_mutex_lock(&memory_lock);int rc=-1;
    struct vm_rg_struct *r=get_symrg_byid(p->mm,reg);
    if(!r||r->rg_end<=r->rg_start||r->vmaid!=id||r->mode_bit!=1||off>=r->rg_end-r->rg_start)goto done;
    struct mm_struct *mm=p->mm;
    addr_t va=r->rg_start+off,e=pte_get_entry(mm,va>>12,p);
    if(!PAGING_PAGE_PRESENT(e)||(e&PAGING_PTE_SWAPPED_MASK))goto done;
    struct framephy_struct *fp=p->krnl->mram->used_fp_list;
    while(fp&&fp->fpn!=PAGING_FPN(e))fp=fp->fp_next;
    if(!fp||fp->owner!=mm)goto done;
    addr_t pa=PAGING_FPN(e)*4096+(va&4095);
    ++mm->walks;mm->pt_accesses+=5;
    struct sc_regs req={.a1=SYSMEM_IO_WRITE,.a2=pa,.a3=(unsigned char)b,.flags=SC_INTERNAL};
    rc=_syscall(p->krnl,p->pid,17,&req);
    if(!rc){++mm->data_accesses;pte_set_entry(mm,va>>12,e|PAGING_PTE_DIRTY_MASK,p);}
done:pthread_mutex_unlock(&memory_lock);return rc;
}

int __read_user_mem(struct pcb_t *p,int id,int reg,addr_t off,BYTE *b){return __read(p,id,reg,off,b);}
int __write_user_mem(struct pcb_t *p,int id,int reg,addr_t off,BYTE b){return __write(p,id,reg,off,b);}
int __read_kernel_mem(struct pcb_t *p,int id,int reg,addr_t off,BYTE *b)
{
    pthread_mutex_lock(&memory_lock);int rc=-1;
    struct vm_rg_struct *r=get_symrg_byid(p->mm,reg);
    if(!r||r->rg_end<=r->rg_start||r->vmaid!=id||r->mode_bit!=0||off>=r->rg_end-r->rg_start||!b)goto done;
    struct mm_struct *mm=p->krnl->mm;
    addr_t va=r->rg_start+off,e=pte_get_entry(mm,va>>12,p);
    if(!PAGING_PAGE_PRESENT(e)||(e&PAGING_PTE_SWAPPED_MASK))goto done;
    struct framephy_struct *fp=p->krnl->mram->used_fp_list;
    while(fp&&fp->fpn!=PAGING_FPN(e))fp=fp->fp_next;
    if(!fp||fp->owner!=mm)goto done;
    addr_t pa=PAGING_FPN(e)*4096+(va&4095);
    ++mm->walks;mm->pt_accesses+=5;
    rc=MEMPHY_read(p->krnl->mram,pa,b);
    if(!rc){++mm->data_accesses;}
done:pthread_mutex_unlock(&memory_lock);return rc;
}

int __write_kernel_mem(struct pcb_t *p,int id,int reg,addr_t off,BYTE b)
{
    pthread_mutex_lock(&memory_lock);int rc=-1;
    struct vm_rg_struct *r=get_symrg_byid(p->mm,reg);
    if(!r||r->rg_end<=r->rg_start||r->vmaid!=id||r->mode_bit!=0||off>=r->rg_end-r->rg_start)goto done;
    struct mm_struct *mm=p->krnl->mm;
    addr_t va=r->rg_start+off,e=pte_get_entry(mm,va>>12,p);
    if(!PAGING_PAGE_PRESENT(e)||(e&PAGING_PTE_SWAPPED_MASK))goto done;
    struct framephy_struct *fp=p->krnl->mram->used_fp_list;
    while(fp&&fp->fpn!=PAGING_FPN(e))fp=fp->fp_next;
    if(!fp||fp->owner!=mm)goto done;
    addr_t pa=PAGING_FPN(e)*4096+(va&4095);
    ++mm->walks;mm->pt_accesses+=5;
    rc=MEMPHY_write(p->krnl->mram,pa,b);
    if(!rc){++mm->data_accesses;pte_set_entry(mm,va>>12,e|PAGING_PTE_DIRTY_MASK,p);}
done:pthread_mutex_unlock(&memory_lock);return rc;
}

/* Public wrappers: khong dua PCB pointer vao kernel dispatcher.
 * Chi cac helper __alloc/__read/... da duoc handler goi moi truy cap MM. */

int liballoc(struct pcb_t *p,addr_t size,uint32_t reg)
{
    struct sc_regs r={.a1=SYSMEM_REGION_ALLOC,.a2=reg,.a3=size,.flags=SC_INTERNAL};
    int rc=_syscall(p->krnl,p->pid,17,&r);
    if(!rc)printf("[ALLOC] PID=%u reg=%u VA=%016"PRIx64" size=%"PRIu64"\n",p->pid,reg,r.a6,size);
    return rc;
}

int libfree(struct pcb_t *p,uint32_t reg)
{
    struct sc_regs r={.a1=SYSMEM_REGION_FREE,.a2=reg,.flags=SC_INTERNAL};return _syscall(p->krnl,p->pid,17,&r);
}

int libread(struct pcb_t *p,uint32_t reg,addr_t off,uint32_t *out)
{
    struct sc_regs r={.a1=SYSMEM_REGION_READ,.a2=reg,.a3=off,.flags=SC_INTERNAL};
    if(!out||_syscall(p->krnl,p->pid,17,&r))return -1;
    *out=(uint32_t)r.a6;printf("[READ] PID=%u reg=%u offset=%"PRIu64" value=%u\n",p->pid,reg,off,*out);return 0;
}

int libwrite(struct pcb_t *p,BYTE b,uint32_t reg,addr_t off)
{
    struct sc_regs r={.a1=SYSMEM_REGION_WRITE,.a2=reg,.a3=off,.a4=(unsigned char)b,.flags=SC_INTERNAL};return _syscall(p->krnl,p->pid,17,&r);
}

/* Kernel co mot page table dung chung. Symbol cua kernel region nam
 * trong PCB chu so huu, ngan process khac truy cap region qua reg trung. */
addr_t __kmalloc(struct pcb_t *p,int id,int reg,addr_t size,addr_t *out)
{
    int rc=-1;pthread_mutex_lock(&memory_lock);struct vm_rg_struct *s=get_symrg_byid(p->mm,reg);
    struct mm_struct *mm=p->krnl->mm;struct vm_area_struct *v=mm->mmap;
    if(!s||(s && s->rg_end>s->rg_start)||id!=0||!out||!size||size>8192ULL*4096)goto done;
    addr_t n=(size+4095)/4096,va=v->vm_end;
    struct vm_rg_struct mapped;
    if(n>(UINT64_MAX-va)/4096||vm_map_kernel(p,v->vm_start,va+n*4096,va,(int)n,&mapped))goto done;
    *s=(struct vm_rg_struct){.vmaid=id,.mode_bit=0,.rg_start=va,.rg_end=va+size};
    v->vm_end=v->sbrk=va+n*4096;*out=va;p->regs[reg]=va;rc=0;
#ifdef IODUMP
    printf("[REGION] PID=%u reg=%d mode=kernel range=[%016"PRIx64",%016"PRIx64")\n",p->pid,reg,s->rg_start,s->rg_end);
#endif

done:pthread_mutex_unlock(&memory_lock);return rc?(addr_t)-1:0;
}
int libkmem_malloc(struct pcb_t *p,uint32_t size,uint32_t reg)
{
    struct sc_regs r={.a1=SYSMEM_KMALLOC,.a2=reg,.a3=size,.flags=SC_INTERNAL};
    if(_syscall(p->krnl,p->pid,17,&r))return -1;
    printf("[KMALLOC] PID=%u reg=%u VA=%016"PRIx64" size=%u\n",p->pid,reg,r.a6,size);return 0;
}

int libkmem_copy_from_user(struct pcb_t *p,uint32_t src,uint32_t dst,uint32_t off,uint32_t size)
{
    struct sc_regs r={.a1=SYSMEM_COPY_FROM,.a2=src,.a3=dst,.a4=off,.a5=size,.flags=SC_INTERNAL};return _syscall(p->krnl,p->pid,17,&r);
}

int libkmem_copy_to_user(struct pcb_t *p,uint32_t src,uint32_t dst,uint32_t off,uint32_t size)
{
    struct sc_regs r={.a1=SYSMEM_COPY_TO,.a2=src,.a3=dst,.a4=off,.a5=size,.flags=SC_INTERNAL};return _syscall(p->krnl,p->pid,17,&r);
}

int libmem_dma_memcpy(struct pcb_t *p,uint32_t src,uint32_t dst,uint32_t off,uint32_t size)
{
    /* DMA RTL thuoc phan co-processor. Khong gia lap thanh cong khi chua tich hop bus. */
    (void)p;(void)src;(void)dst;(void)off;(void)size;return -ENOSYS;
}
int free_pcb_memph(struct pcb_t *p)
{
    if(!p||!p->mm)return -1;
    pthread_mutex_lock(&memory_lock);
    /* Bo PID khoi registry truoc khi huy MM. */
    struct pcb_t **q=&p->krnl->processes;while(*q&&*q!=p)q=&(*q)->registry_next;
    if(*q)*q=p->registry_next;
    p->registry_next=NULL;
    for(int i=0;i<REG_COUNT;++i){struct vm_rg_struct *r=get_symrg_byid(p->mm,i);if(r&&r->rg_end>r->rg_start&&!r->mode_bit)__free(p,0,i);}
    struct mm_struct *mm=p->mm;
    /* In thong ke truoc khi giai phong bang trang. */
#ifdef PAGETBL_DUMP
    print_pgtbl(p,0,UINT64_MAX);
#else
    print_pgtbl(p,1,0);
#endif
    /* DFS lap postorder: free frame o la, free bang khi da tham het. */
    addr_t *t[5]={mm->pgd};int next[5]={0},d=0;
    if(t[0])while(d>=0){
        if(next[d]==512){free(t[d]);--d;continue;}
        int i=next[d]++;if(!t[d][i])continue;
        if(d<4){t[d+1]=(addr_t *)(uintptr_t)t[d][i];next[++d]=0;}
        else if(PAGING_PAGE_PRESENT(t[d][i]))MEMPHY_put_freefp(p->krnl->mram,PAGING_FPN(t[d][i]));
    }
    while(mm->mmap){struct vm_area_struct *v=mm->mmap;mm->mmap=v->vm_next;
        while(v->vm_freerg_list){struct vm_rg_struct *r=v->vm_freerg_list;v->vm_freerg_list=r->rg_next;free(r);}free(v);}
    while(mm->fifo_pgn){struct pgn_t *n=mm->fifo_pgn;mm->fifo_pgn=n->pg_next;free(n);}
    free(mm);p->mm=NULL;
    pthread_mutex_unlock(&memory_lock);return 0;
}

int find_victim_page(struct mm_struct *mm,addr_t *out)
{
    if(!mm||!out||!mm->fifo_pgn)return -1;
    struct pgn_t **p=&mm->fifo_pgn;while((*p)->pg_next)p=&(*p)->pg_next;
    *out=(*p)->pgn;free(*p);*p=NULL;return 0;
}
