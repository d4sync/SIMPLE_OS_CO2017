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
#include "dma.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>

#define REG_COUNT 10
static void trace_regions(struct pcb_t *p)
{
#ifdef IODUMP
    struct vm_area_struct *v=p->mm->mmap;
    printf("[VMA] PID=%u start=%016"PRIx64" end=%016"PRIx64" sbrk=%016"PRIx64"\n",p->pid,v->vm_start,v->vm_end,v->sbrk);
    for(int i=0;i<REG_COUNT;++i){
        struct vm_rg_struct *r=&p->mm->symrgtbl[i];
        if(r->rg_end>r->rg_start)printf("  reg=%d mode=%lu range=[%016"PRIx64",%016"PRIx64")\n",i,r->mode_bit,r->rg_start,r->rg_end);
    }
    for(struct vm_rg_struct *r=v->vm_freerg_list;r;r=r->rg_next)
        printf("  free=[%016"PRIx64",%016"PRIx64")\n",r->rg_start,r->rg_end);
#else
    (void)p;
#endif
}
static int valid_id(int id){return id>=0 && id<REG_COUNT;}
static int live(struct vm_rg_struct *r){return r && r->rg_end>r->rg_start;}
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm,int id)
{
    return mm && valid_id(id) ? &mm->symrgtbl[id]:NULL;
}
int enlist_vm_freerg_list(struct mm_struct *mm,struct vm_rg_struct *r)
{
    return vm_insert_free(get_vma_by_num(mm,r->vmaid),r);
}
int get_free_vmrg_area(struct pcb_t *p,int id,addr_t size,struct vm_rg_struct *r)
{
    struct vm_area_struct *v=get_vma_by_num(p->mm,id);
    if(!v||!size||!r)return -1;
    struct vm_rg_struct **q=&v->vm_freerg_list;
    for(;*q;q=&(*q)->rg_next){struct vm_rg_struct *f=*q;
        if(size>f->rg_end-f->rg_start)continue;
        *r=(struct vm_rg_struct){.vmaid=id,.mode_bit=1,.rg_start=f->rg_start,.rg_end=f->rg_start+size};
        f->rg_start+=size;
        if(f->rg_start==f->rg_end){*q=f->rg_next;free(f);}
        return 0;
    }
    return -1;
}

/* Libmem la wrapper; tham so qua syscall chi la PID va thanh ghi. */
static int memcall(struct pcb_t *p,arg_t op,arg_t a2,arg_t a3,arg_t *out)
{
    struct sc_regs r={.a1=op,.a2=a2,.a3=a3,.flags=SC_INTERNAL};
    int rc=_syscall(p->krnl,p->pid,17,&r);
    if(!rc && out)*out=r.a3;
    return rc;
}
int __alloc(struct pcb_t *p,int id,int reg,addr_t size,addr_t *out)
{
    int rc=-1;mm_lock();
    struct vm_rg_struct *s=get_symrg_byid(p->mm,reg),r;
    struct vm_area_struct *v=get_vma_by_num(p->mm,id);
    if(!s||live(s)||!v||!out||!size)goto done;
    if(get_free_vmrg_area(p,id,size,&r)){
        /* Neu tail da co mot phan free, chi xin them phan thieu. */
        addr_t missing=size;
        for(struct vm_rg_struct *f=v->vm_freerg_list;f;f=f->rg_next)
            if(f->rg_end==v->vm_end && f->rg_end-f->rg_start<size)missing-=f->rg_end-f->rg_start;
        if(memcall(p,SYSMEM_INC_OP,id,missing,NULL)||get_free_vmrg_area(p,id,size,&r))goto done;
    }
    /* Xoa data cu khi tai su dung free region, tranh ro ri noi dung. */
    for(addr_t a=r.rg_start;a<r.rg_end;){addr_t pa;
        if(mm_translate_fault(p->krnl,p->mm,a,&pa,0))goto done;
        addr_t n=4096-(a&4095);if(n>r.rg_end-a)n=r.rg_end-a;
        memset(p->krnl->mram->storage+pa,0,n);a+=n;p->mm->data_accesses+=n;
    }
    *s=r;*out=r.rg_start;p->regs[reg]=r.rg_start;rc=0;
    trace_regions(p);
done:mm_unlock();return rc;
}

/* FREE user tra region ve free list; frame giu lai de tai su dung.
 * FREE kernel bo mapping va tra ca day frame ngay. */
int __free(struct pcb_t *p,int id,int reg)
{
    int rc=-1;mm_lock();struct vm_rg_struct *s=get_symrg_byid(p->mm,reg);
    if(!live(s)||s->vmaid!=id)goto done;
    if(s->mode_bit){struct vm_rg_struct *r=malloc(sizeof(*r));if(!r)goto done;*r=*s;r->rg_next=NULL;
        if(enlist_vm_freerg_list(p->mm,r)){free(r);goto done;}
    }else{
        addr_t n=(s->rg_end-s->rg_start+4095)/4096;
        mm_unmap_pages(p,p->krnl->mm,s->rg_start,n);
    }
    memset(s,0,sizeof(*s));p->regs[reg]=0;rc=0;
    trace_regions(p);
done:mm_unlock();return rc;
}
static int region_ok(struct vm_rg_struct *r,int id,unsigned mode,addr_t off,addr_t size)
{
    return live(r)&&r->vmaid==id&&r->mode_bit==mode&&off<=r->rg_end-r->rg_start&&size<=r->rg_end-r->rg_start-off;
}
/* Moi byte: walk 5 cap, kiem tra mode/owner, sau do truy cap RAM.
 * User IO phai qua syscall; kernel IO khong lo dia chi vat ly ra user. */
static int access_byte(struct pcb_t *p,int id,int reg,unsigned mode,addr_t off,BYTE *b,int write)
{
    struct vm_rg_struct *r=get_symrg_byid(p->mm,reg);
    if(!b||!region_ok(r,id,mode,off,1))return -1;
    struct mm_struct *mm=mode?p->mm:p->krnl->mm;
    addr_t pa;int rc;
    /* Page fault: a swapped-out page is brought back before the access. */
    if(mm_translate_fault(p->krnl,mm,r->rg_start+off,&pa,write)||!MEMPHY_owned(p->krnl->mram,pa/4096,mm))return -1;
    if(mode){arg_t data=0;rc=memcall(p,write?SYSMEM_IO_WRITE:SYSMEM_IO_READ,pa,write?(unsigned char)*b:0,&data);if(!rc&&!write)*b=(BYTE)data;}
    else rc=write?MEMPHY_write(p->krnl->mram,pa,*b):MEMPHY_read(p->krnl->mram,pa,b);
    if(!rc)++mm->data_accesses;
    return rc;
}
int __read(struct pcb_t *p,int id,int reg,addr_t off,BYTE *b){mm_lock();int rc=access_byte(p,id,reg,1,off,b,0);mm_unlock();return rc;}
int __write(struct pcb_t *p,int id,int reg,addr_t off,BYTE b){mm_lock();int rc=access_byte(p,id,reg,1,off,&b,1);mm_unlock();return rc;}
int __read_user_mem(struct pcb_t *p,int id,int reg,addr_t off,BYTE *b){return __read(p,id,reg,off,b);}
int __write_user_mem(struct pcb_t *p,int id,int reg,addr_t off,BYTE b){return __write(p,id,reg,off,b);}
int __read_kernel_mem(struct pcb_t *p,int id,int reg,addr_t off,BYTE *b){mm_lock();int rc=access_byte(p,id,reg,0,off,b,0);mm_unlock();return rc;}
int __write_kernel_mem(struct pcb_t *p,int id,int reg,addr_t off,BYTE b){mm_lock();int rc=access_byte(p,id,reg,0,off,&b,1);mm_unlock();return rc;}

/* Public wrappers: khong dua PCB pointer vao kernel dispatcher.
 * Chi cac helper __alloc/__read/... da duoc handler goi moi truy cap MM. */
static int region_call(struct pcb_t *p, arg_t op, arg_t a2, arg_t a3,
                       arg_t a4, arg_t a5, arg_t *result)
{
    struct sc_regs r = {.a1=op,.a2=a2,.a3=a3,.a4=a4,.a5=a5,
                        .flags=SC_INTERNAL};
    int rc = _syscall(p->krnl,p->pid,17,&r);
    if (!rc && result) *result=r.a6;
    return rc;
}
int liballoc(struct pcb_t *p,addr_t size,uint32_t reg)
{
    arg_t a=0;
    int rc=region_call(p,SYSMEM_REGION_ALLOC,reg,size,0,0,&a);
    if(!rc)printf("[ALLOC] PID=%u reg=%u VA=%016"PRIx64" size=%"PRIu64"\n",p->pid,reg,a,size);
    return rc;
}
int libfree(struct pcb_t *p,uint32_t reg)
{
    return region_call(p,SYSMEM_REGION_FREE,reg,0,0,0,NULL);
}
int libread(struct pcb_t *p,uint32_t reg,addr_t off,uint32_t *out)
{
    arg_t data=0;
    if(!out||region_call(p,SYSMEM_REGION_READ,reg,off,0,0,&data))return -1;
    *out=(uint32_t)data;
    printf("[READ] PID=%u reg=%u offset=%"PRIu64" value=%u\n",p->pid,reg,off,*out);
    return 0;
}
int libwrite(struct pcb_t *p,BYTE b,uint32_t reg,addr_t off)
{
    return region_call(p,SYSMEM_REGION_WRITE,reg,off,(unsigned char)b,0,NULL);
}

/* Kernel co mot page table dung chung. Symbol cua kernel region nam
 * trong PCB chu so huu, ngan process khac truy cap region qua reg trung. */
addr_t __kmalloc(struct pcb_t *p,int id,int reg,addr_t size,addr_t *out)
{
    int rc=-1;mm_lock();struct vm_rg_struct *s=get_symrg_byid(p->mm,reg);
    struct mm_struct *mm=p->krnl->mm;struct vm_area_struct *v=mm->mmap;
    if(!s||live(s)||id!=0||!out||!size||size>8192ULL*4096)goto done;
    addr_t n=(size+4095)/4096,va=v->vm_end;
    struct vm_rg_struct mapped;
    if(n>(UINT64_MAX-va)/4096||vm_map_kernel(p,v->vm_start,va+n*4096,va,(int)n,&mapped))goto done;
    *s=(struct vm_rg_struct){.vmaid=id,.mode_bit=0,.rg_start=va,.rg_end=va+size};
    v->vm_end=v->sbrk=va+n*4096;*out=va;p->regs[reg]=va;rc=0;
    trace_regions(p);
    mm_stats(mm,p->pid,"kernel-shared");
done:mm_unlock();return rc?(addr_t)-1:0;
}
int libkmem_malloc(struct pcb_t *p,addr_t size,uint32_t reg)
{
    arg_t a=0;if(region_call(p,SYSMEM_KMALLOC,reg,size,0,0,&a))return -1;
    printf("[KMALLOC] PID=%u reg=%u VA=%016"PRIx64" size=%"PRIu64"\n",p->pid,reg,a,size);return 0;
}
int mm_copy_region(struct pcb_t *p,uint32_t src,uint32_t dst,addr_t off,addr_t size,unsigned smode)
{
    int rc=-1;mm_lock();struct vm_rg_struct *s=get_symrg_byid(p->mm,src),*d=get_symrg_byid(p->mm,dst);
    if(!region_ok(s,0,smode,off,size)||!region_ok(d,0,!smode,off,size))goto done;
    /* Validate toan bo khoang truoc khi copy, khong ghi mot phan vi vuot bien. */
    for(addr_t i=0;i<size;++i){BYTE b;
        if(access_byte(p,0,src,smode,off+i,&b,0)||access_byte(p,0,dst,!smode,off+i,&b,1))goto done;
    }
    rc=0;
done:mm_unlock();return rc;
}
int libkmem_copy_from_user(struct pcb_t *p,uint32_t src,uint32_t dst,addr_t off,addr_t size){return region_call(p,SYSMEM_COPY_FROM,src,dst,off,size,NULL);}
int libkmem_copy_to_user(struct pcb_t *p,uint32_t src,uint32_t dst,addr_t off,addr_t size){return region_call(p,SYSMEM_COPY_TO,src,dst,off,size,NULL);}
/*
 * DMA_MEMCPY (asynchronous)
 * The instruction only submits the request through syscall 17 and returns.
 * The DMA worker thread drives the RTL engine on physical addresses; the CPU
 * keeps running other work. The PID's next memory instruction waits (see
 * cpu.c) until the completion callback has released the pinned frames.
 */
int libmem_dma_memcpy(struct pcb_t *p,uint32_t src,uint32_t dst,addr_t off,addr_t size)
{
    int rc=region_call(p,SYSMEM_DMA_OP,src,dst,off,size,NULL);
    if(!rc)printf("[DMA-SUBMIT] PID=%u src_reg=%u dst_reg=%u offset=%"PRIu64" size=%"PRIu64"\n",
                  p->pid,src,dst,off,size);
    return rc;
}

/* Frames used by one DMA request; kept pinned until the engine is done. */
struct dma_pin {
    struct krnl_t *krnl;
    uint32_t pid;
    struct dma_span *spans;
    size_t nspans;
    addr_t *frames;
    size_t nframes;
};

static void dma_unpin(struct dma_pin *c)
{
    for(size_t i=0;i<c->nframes;++i)MEMPHY_pin(c->krnl->mram,c->frames[i],-1);
    free(c->spans);free(c->frames);free(c);
}

/* Completion callback, runs on the DMA worker thread. */
static void dma_release(void *cookie,int status)
{
    struct dma_pin *c=cookie;
    uint32_t pid=c->pid;
    mm_lock();dma_unpin(c);mm_unlock();
    /* -EBUSY: channel owned by another PID, the CPU retries the instruction. */
    if(status!=-EBUSY)printf("[DMA-DONE] PID=%u status=%s\n",pid,status?"failed":"ok");
}

/* Translate (faulting pages in if needed) and pin one page of a region. */
static int dma_pin_page(struct dma_pin *c,struct mm_struct *mm,addr_t va,int write,addr_t *pa)
{
    if(mm_translate_fault(c->krnl,mm,va,pa,write)||
       !MEMPHY_owned(c->krnl->mram,*pa/4096,mm)||
       MEMPHY_pin(c->krnl->mram,*pa/4096,1))return -1;
    c->frames[c->nframes++]=*pa/4096;
    return 0;
}

/* dma_mapper for src/dma.c. Called inside syscall 17 (mm_lock held).
 * Splits the copy at every page boundary of either side, because frames
 * of a region are not physically contiguous, and merges adjacent runs. */
int mm_dma_map(struct krnl_t *k,uint32_t pid,uint32_t src,uint32_t dst,
               uint32_t off,uint32_t bytes,struct dma_mapping *m)
{
    int rc=-EINVAL;
    mm_lock();
    struct pcb_t *p=mm_lookup(k,pid);
    struct vm_rg_struct *s=p?get_symrg_byid(p->mm,src):NULL,*d=p?get_symrg_byid(p->mm,dst):NULL;
    if(!m||!bytes||!live(s)||!live(d)||s==d||
       off>s->rg_end-s->rg_start||bytes>s->rg_end-s->rg_start-off||
       off>d->rg_end-d->rg_start||bytes>d->rg_end-d->rg_start-off){mm_unlock();return rc;}
    struct mm_struct *ms=s->mode_bit?p->mm:k->mm,*md=d->mode_bit?p->mm:k->mm;
    size_t cap=2*((size_t)bytes/4096)+4;
    struct dma_pin *c=calloc(1,sizeof(*c));
    if(c){c->krnl=k;c->pid=pid;c->spans=calloc(cap,sizeof(*c->spans));c->frames=calloc(2*cap,sizeof(*c->frames));}
    if(!c||!c->spans||!c->frames){if(c){free(c->spans);free(c->frames);free(c);}mm_unlock();return -ENOMEM;}
    for(addr_t i=0;i<bytes;){
        addr_t vs=s->rg_start+off+i,vd=d->rg_start+off+i,ps,pd;
        addr_t n=4096-(vs&4095);
        if(4096-(vd&4095)<n)n=4096-(vd&4095);
        if(bytes-i<n)n=bytes-i;
        /* Source first; once pinned it cannot be evicted by the dst fault. */
        if(dma_pin_page(c,ms,vs,0,&ps)||dma_pin_page(c,md,vd,1,&pd)){dma_unpin(c);mm_unlock();return -EFAULT;}
        struct dma_span *last=c->nspans?&c->spans[c->nspans-1]:NULL;
        if(last&&last->src+last->bytes==ps&&last->dst+last->bytes==pd)last->bytes+=n;
        else c->spans[c->nspans++]=(struct dma_span){ps,pd,n};
        i+=n;
    }
    m->spans=c->spans;m->count=c->nspans;m->release=dma_release;m->cookie=c;
    mm_unlock();
    return 0;
}
int free_pcb_memph(struct pcb_t *p)
{
    mm_lock();
    for(int i=0;i<REG_COUNT;++i){struct vm_rg_struct *r=get_symrg_byid(p->mm,i);if(live(r)&&!r->mode_bit)__free(p,0,i);}
    #ifdef PAGETBL_DUMP
    print_pgtbl(p,0,UINT64_MAX);
#endif
    mm_stats(p->mm,p->pid,"user-final");
    mm_destroy(p->mm,p->krnl->mram);free(p->mm);p->mm=NULL;
    mm_unlock();return 0;
}
int find_victim_page(struct mm_struct *mm,addr_t *out)
{
    if(!mm||!out||!mm->fifo_pgn)return -1;
    struct pgn_t **p=&mm->fifo_pgn;while((*p)->pg_next)p=&(*p)->pg_next;
    *out=(*p)->pgn;free(*p);*p=NULL;return 0;
}
