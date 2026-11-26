/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

#include "mm64.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <inttypes.h>
#include <limits.h>

#ifdef MM64
/* Mot khoa de serialize transaction VM, syscall va thu hoi PCB.
 * Recursive vi libmem -> syscall -> handler cung nam trong transaction. */
static pthread_mutex_t memory_lock;
static pthread_once_t lock_once = PTHREAD_ONCE_INIT;
static void init_lock(void)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&memory_lock, &attr);
    pthread_mutexattr_destroy(&attr);
}
void mm_lock(void) { pthread_once(&lock_once, init_lock); pthread_mutex_lock(&memory_lock); }
void mm_unlock(void) { pthread_mutex_unlock(&memory_lock); }

int mm_canonical(addr_t va)
{
    return (va >> 57) == (((va >> 56) & 1) ? 127 : 0);
}

int get_pd_from_address(addr_t va, addr_t *pgd, addr_t *p4d, addr_t *pud,
                        addr_t *pmd, addr_t *pt)
{
    if (!mm_canonical(va)) return -1;
    *pgd = (va >> 48) & 511; *p4d = (va >> 39) & 511;
    *pud = (va >> 30) & 511; *pmd = (va >> 21) & 511;
    *pt = (va >> 12) & 511;
    return 0;
}
static int va_from_pgn(addr_t pgn, addr_t *va)
{
    if (pgn > (UINT64_MAX >> 12)) return -1;
    *va = pgn << 12;
    /* Cho phep page number 45 bit cua dia chi canonical nua tren. */
    if (pgn < PAGING64_MAX_PGN && (*va & (UINT64_C(1) << 56)))
        *va |= UINT64_C(0xfe00000000000000);
    return mm_canonical(*va) ? 0 : -1;
}
int get_pd_from_pagenum(addr_t pgn, addr_t *pgd, addr_t *p4d, addr_t *pud,
                        addr_t *pmd, addr_t *pt)
{
    addr_t va;
    if (va_from_pgn(pgn, &va)) return -1;
    return get_pd_from_address(va, pgd, p4d, pud, pmd, pt);
}

/* Moi directory gom 512 entry 8 byte. 4 cap tren chua host pointer,
 * cap cuoi chua PTE 32 bit trong slot 64 bit. Khong cap phat bang phang. */
static addr_t *new_table(struct mm_struct *mm, int level)
{
    addr_t *p = calloc(MM64_ENTRIES, sizeof(*p));
    if (p) {
        ++mm->tables[level];
        uint64_t n=0;
        for(int i=0;i<5;++i)n+=mm->tables[i];
        if(n>mm->peak_tables)mm->peak_tables=n;
    }
    return p;
}
static addr_t *walk(struct mm_struct *mm, addr_t va, int create, int count)
{
    addr_t idx[5];
    if (!mm || !mm->pgd || get_pd_from_address(va, idx, idx+1, idx+2, idx+3, idx+4)) return NULL;
    if (count) ++mm->walks;
    addr_t *table = mm->pgd;
    for (int l = 0; l < 4; ++l) {
        if (count) ++mm->pt_accesses;
        if (!table[idx[l]]) {
            if (!create) return NULL;
            addr_t *child = new_table(mm, 3-l);
            if (!child) return NULL;
            table[idx[l]] = (addr_t)(uintptr_t)child;
        }
        table = (addr_t *)(uintptr_t)table[idx[l]];
    }
    if (count) ++mm->pt_accesses;
    return &table[idx[4]];
}
static int empty_table(addr_t *t)
{
    for (int i=0; i<512; ++i) if (t[i]) return 0;
    return 1;
}
static void prune(struct mm_struct *mm, addr_t va)
{
    addr_t idx[5], *t[5];
    if (get_pd_from_address(va, idx, idx+1, idx+2, idx+3, idx+4)) return;
    t[0]=mm->pgd;
    int depth=0;
    while (depth<4 && t[depth][idx[depth]]) {
        t[depth+1]=(addr_t *)(uintptr_t)t[depth][idx[depth]]; ++depth;
    }
    for (int l=depth; l>0 && empty_table(t[l]); --l) {
        free(t[l]); --mm->tables[4-l]; t[l-1][idx[l-1]]=0;
    }
}
int init_pte(addr_t *pte, int pre, addr_t fpn, int drt, int swp, int type, addr_t off)
{
    if (!pte) return -1;
    if (swp) {
        if (type<0 || type>=PAGING_MAX_MMSWP || off>(PAGING_PTE_SWPOFF_MASK>>5)) return -1;
        *pte=PAGING_PTE_SWAPPED_MASK | ((addr_t)type) | (off<<5);
    } else if (pre) {
        if (fpn>PAGING_PTE_FPN_MASK) return -1;
        *pte=PAGING_PTE_PRESENT_MASK | fpn | (drt ? PAGING_PTE_DIRTY_MASK:0);
    } else *pte=0;
    return 0;
}
addr_t pte_get_entry(struct mm_struct *mm, addr_t pgn, struct pcb_t *caller)
{
    (void)caller;
    addr_t va;
    if (va_from_pgn(pgn,&va)) return 0;
    addr_t *e=walk(mm,va,0,0); return e ? *e:0;
}
int pte_set_entry(struct mm_struct *mm, addr_t pgn, uint64_t value, struct pcb_t *caller)
{
    (void)caller;
    addr_t va;
    if (va_from_pgn(pgn,&va) || value>UINT32_MAX) return -1;
    addr_t *e=walk(mm,va,value!=0,0);
    if (!e) { if (mm && mm->pgd) prune(mm,va); return value ? -1:0; }
    *e=value;
    if (!value) prune(mm,va);
    return 0;
}
int pte_set_fpn(struct mm_struct *mm, addr_t pgn, addr_t fpn, struct pcb_t *caller)
{
    addr_t e;
    if (init_pte(&e,1,fpn,0,0,0,0)) return -1;
    return pte_set_entry(mm,pgn,e,caller);
}
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int type, addr_t off)
{
    addr_t e;
    if (!caller || init_pte(&e,0,0,0,1,type,off)) return -1;
    return pte_set_entry(caller->mm,pgn,e,caller);
}

/*
 * Page replacement
 * ----------------
 * Policy : FIFO over every user page resident in RAM (global, all PIDs).
 *          Kernel pages and frames pinned by an ongoing DMA are never
 *          chosen. The age of a page is the time it entered RAM.
 * Swap out: copy the victim frame to a free slot of the active SWAP device,
 *          rewrite the victim PTE as "swapped" (type = swap device id,
 *          offset = slot), then release the RAM frame.
 * Swap in : on access to a swapped PTE, get a RAM frame (evicting another
 *          page if needed), copy the slot back, free the slot and mark the
 *          PTE present again.
 * Every caller already holds mm_lock (all memory work runs inside the
 * syscall 17 handlers), so no extra locking is needed here.
 */
static struct krnl_t *swap_krnl;

void mm_swap_init(struct krnl_t *kernel)
{
    swap_krnl = kernel;
}

static int swap_enabled(struct krnl_t *k)
{
    return k && k == swap_krnl && k->mram && k->active_mswp &&
           k->active_mswp->maxsz >= PAGING64_PAGESZ;
}

/* PID owning MM, only used to make the trace readable. */
static unsigned pid_of_mm(struct krnl_t *k, struct mm_struct *mm)
{
    for (struct pcb_t *p = k ? k->processes : NULL; p; p = p->registry_next)
        if (p->mm == mm)
            return p->pid;
    return 0;
}

static int swap_out_one(struct krnl_t *k)
{
    struct memphy_struct *ram = k->mram, *swp = k->active_mswp;
    struct framephy_struct *victim = MEMPHY_pick_victim(ram, k->mm);
    addr_t slot, entry, va;

    if (!victim)
        return -1;
    if (MEMPHY_get_freefp(swp, &slot))
        return -1;                       /* SWAP device is full. */
    if (__swap_cp_page(ram, victim->fpn, swp, slot) ||
        init_pte(&entry, 0, 0, 0, 1, k->active_mswp_id, slot) ||
        pte_set_entry(victim->owner, victim->pgn, entry, NULL)) {
        MEMPHY_put_freefp(swp, slot);
        return -1;
    }

    struct mm_struct *mm = victim->owner;
    addr_t fpn = victim->fpn;
    MEMPHY_set_owner(swp, slot, mm);
    ++mm->swap_outs;
    if (va_from_pgn(victim->pgn, &va))
        va = victim->pgn << 12;
#ifdef IODUMP
    printf("[SWAP-OUT] PID=%u VA=%016" PRIx64 " FPN=%" PRIu64
           " -> SWP%u slot=%" PRIu64 "\n",
           pid_of_mm(k, mm), va, fpn, k->active_mswp_id, slot);
#endif
    MEMPHY_put_freefp(ram, fpn);
    return 0;
}

/* RAM frame for a user page. Evict a victim when RAM is exhausted. Frames
 * above the 13-bit FPN limit of the PTE format are treated as unusable. */
static int get_user_frame(struct krnl_t *k, addr_t *fpn)
{
    for (int tries = 0; tries < 2; ++tries) {
        if (!MEMPHY_get_freefp(k->mram, fpn)) {
            if (*fpn <= PAGING_PTE_FPN_MASK)
                return 0;
            MEMPHY_put_freefp(k->mram, *fpn);
        }
        if (!swap_enabled(k) || swap_out_one(k))
            return -1;
    }
    return -1;
}

static void free_swap_slot(struct krnl_t *k, addr_t entry)
{
    unsigned type = PAGING_PTE_SWPTYP(entry);
    if (k && k->mswp && type < PAGING_MAX_MMSWP && k->mswp[type])
        MEMPHY_put_freefp(k->mswp[type], PAGING_PTE_SWP(entry));
}

static int swap_in(struct krnl_t *k, struct mm_struct *mm, addr_t va)
{
    addr_t *e = walk(mm, va, 0, 0), entry, fpn, present;
    if (!e || PAGING_PAGE_PRESENT(*e) || !(*e & PAGING_PTE_SWAPPED_MASK))
        return -1;
    entry = *e;

    unsigned type = PAGING_PTE_SWPTYP(entry);
    addr_t slot = PAGING_PTE_SWP(entry);
    if (type >= PAGING_MAX_MMSWP || !k->mswp[type])
        return -1;
    /* May evict another page; never this one (it is not in RAM). */
    if (get_user_frame(k, &fpn))
        return -1;
    if (__swap_cp_page(k->mswp[type], slot, k->mram, fpn) ||
        init_pte(&present, 1, fpn, 0, 0, 0, 0) ||
        pte_set_entry(mm, va >> 12, present, NULL)) {
        MEMPHY_put_freefp(k->mram, fpn);
        return -1;
    }
    MEMPHY_put_freefp(k->mswp[type], slot);
    MEMPHY_track(k->mram, fpn, mm, va >> 12);
    ++mm->swap_ins;
#ifdef IODUMP
    printf("[SWAP-IN]  PID=%u VA=%016" PRIx64 " SWP%u slot=%" PRIu64
           " -> FPN=%" PRIu64 "\n", pid_of_mm(k, mm), va & ~UINT64_C(4095),
           type, slot, fpn);
#endif
    return 0;
}

int mm_translate_fault(struct krnl_t *k, struct mm_struct *mm, addr_t va,
                       addr_t *pa, int write)
{
    addr_t *e = walk(mm, va, 0, 0);
    if (e && !PAGING_PAGE_PRESENT(*e) && (*e & PAGING_PTE_SWAPPED_MASK)) {
        if (!swap_enabled(k) || swap_in(k, mm, va))
            return -1;
    }
    return mm_translate(mm, va, pa, write);
}

/* Rollback neu RAM het hoac calloc bang trang that bai. FPN 13 bit
 * gioi han RAM anh xa 8192 frame; khong cat mat bit cao cua FPN. */
int mm_map_pages(struct pcb_t *proc, struct mm_struct *mm, addr_t va, addr_t count, int contiguous)
{
    if (!proc || !mm || !count || (va & 4095) || count > 8192 ||
        count-1 > (UINT64_MAX-va)/4096 || !mm_canonical(va) ||
        !mm_canonical(va+(count-1)*4096)) return -1;
    struct memphy_struct *ram=proc->krnl->mram;
    for (addr_t i=0;i<count;++i) if (pte_get_entry(mm,(va>>12)+i,proc)) return -1;
    addr_t first=0, i=0;
    if (contiguous && MEMPHY_get_contiguous(ram,count,&first)) return -1;
    for (;i<count;++i) {
        addr_t fpn=first+i;
        /* User pages may evict an older page to SWAP (page replacement). */
        if (!contiguous && get_user_frame(proc->krnl,&fpn)) break;
        if (fpn>PAGING_PTE_FPN_MASK || pte_set_fpn(mm,(va>>12)+i,fpn,proc)) {
            if (!contiguous) MEMPHY_put_freefp(ram,fpn);
            break;
        }
        MEMPHY_track(ram,fpn,mm,(va>>12)+i);
    }
    if (i==count) return 0;
    mm_unmap_pages(proc,mm,va,i);
    if (contiguous) for (;i<count;++i) MEMPHY_put_freefp(ram,first+i);
    return -1;
}
int mm_unmap_pages(struct pcb_t *proc, struct mm_struct *mm, addr_t va, addr_t count)
{
    for (addr_t i=0;i<count;++i) {
        addr_t pgn=(va>>12)+i, e=pte_get_entry(mm,pgn,proc);
        if (PAGING_PAGE_PRESENT(e)) MEMPHY_put_freefp(proc->krnl->mram,PAGING_FPN(e));
        else if (e & PAGING_PTE_SWAPPED_MASK) free_swap_slot(proc->krnl,e);
        pte_set_entry(mm,pgn,0,proc);
    }
    return 0;
}
int mm_translate(struct mm_struct *mm, addr_t va, addr_t *pa, int write)
{
    addr_t *e=walk(mm,va,0,1);
    if (!e || !PAGING_PAGE_PRESENT(*e) || (*e & PAGING_PTE_SWAPPED_MASK)) return -1;
    *pa=PAGING_FPN(*e)*4096+(va&4095);
    if (write) *e |= PAGING_PTE_DIRTY_MASK;
    return 0;
}
int vmap_pgd_memset(struct pcb_t *caller, addr_t va, int count)
{
    /* MAP: neu bat dau tai break, mo rong VMA va map cac frame moi.
     * Neu da map, zero-fill cac page user cua chinh process.
     * Khong cho user tuy y ghi PTE hay map kernel space. */
    if (!caller || count<=0 || (va&4095) || count>8192 ||
        !mm_canonical(va) || va>=caller->krnl->kernel_base ||
        (addr_t)count > (caller->krnl->kernel_base-va)/4096) return -1;
    struct vm_area_struct *v=get_vma_by_num(caller->mm,0);
    if (!v) return -1;
    addr_t bytes=(addr_t)count*4096;
    if (va==v->vm_end) return inc_vma_limit(caller,0,bytes);
    if (va<v->vm_start || va>v->vm_end || bytes>v->vm_end-va) return -1;
    /* Every page must be mapped (present or swapped) before touching any. */
    for (int i=0;i<count;++i)
        if (!pte_get_entry(caller->mm,(va>>12)+i,caller)) return -1;
    /* Fault in and clear one page at a time: faulting page i+1 may evict
     * page i, so translate and clear in the same step. */
    for (int i=0;i<count;++i) {
        addr_t pa;
        if (mm_translate_fault(caller->krnl,caller->mm,va+(addr_t)i*4096,&pa,1) ||
            !MEMPHY_owned(caller->krnl->mram,pa/4096,caller->mm)) return -1;
        memset(caller->krnl->mram->storage+pa,0,4096);
        caller->mm->data_accesses+=4096;
    }
    return 0;
}
addr_t alloc_pages_range(struct pcb_t *proc, int count, struct framephy_struct **list)
{
    if (!proc || !list || count<0 || count>8192) return (addr_t)-1;
    *list=NULL; struct framephy_struct **tail=list;
    for (int i=0;i<count;++i) {
        struct framephy_struct *p=calloc(1,sizeof(*p));
        if (!p) goto fail;
        if (MEMPHY_get_freefp(proc->krnl->mram,&p->fpn)) {free(p); goto fail;}
        if (p->fpn>PAGING_PTE_FPN_MASK) {MEMPHY_put_freefp(proc->krnl->mram,p->fpn);free(p);goto fail;}
        *tail=p;tail=&p->fp_next;
    }
    return 0;
fail:
    while (*list) {struct framephy_struct *p=*list;*list=p->fp_next;MEMPHY_put_freefp(proc->krnl->mram,p->fpn);free(p);}
    return (addr_t)-1;
}
addr_t vmap_page_range(struct pcb_t *proc, addr_t va, int count, struct framephy_struct *frames, struct vm_rg_struct *rg)
{
    if (!proc || !rg || count<=0 || count>8192 || (va&4095) || (addr_t)count>(UINT64_MAX-va)/4096) return (addr_t)-1;
    struct framephy_struct *p=frames;
    for (int i=0;i<count;++i) {
        if (!p || p->fpn>PAGING_PTE_FPN_MASK || pte_get_entry(proc->mm,(va>>12)+i,proc)) return (addr_t)-1;
        p=p->fp_next;
    }
    int i=0;
    for (p=frames;i<count;p=p->fp_next,++i) {
        if (pte_set_fpn(proc->mm,(va>>12)+i,p->fpn,proc)) break;
        MEMPHY_track(proc->krnl->mram,p->fpn,proc->mm,(va>>12)+i);
    }
    if (i<count) { for(int j=0;j<i;++j) pte_set_entry(proc->mm,(va>>12)+j,0,proc); return (addr_t)-1; }
    *rg=(struct vm_rg_struct){.mode_bit=1,.rg_start=va,.rg_end=va+(addr_t)count*4096};
    return 0;
}
addr_t vm_map_ram(struct pcb_t *proc, addr_t start, addr_t end, addr_t va, int count, struct vm_rg_struct *rg)
{
    if (!rg || count<=0 || va<start || va>end || (addr_t)count>(end-va)/4096 || mm_map_pages(proc,proc->mm,va,count,0)) return (addr_t)-1;
    *rg=(struct vm_rg_struct){.mode_bit=1,.rg_start=va,.rg_end=va+(addr_t)count*4096};return 0;
}
addr_t vm_map_kernel(struct pcb_t *proc, addr_t start, addr_t end, addr_t va, int count, struct vm_rg_struct *rg)
{
    if (!rg || count<=0 || va<start || va>end || (addr_t)count>(end-va)/4096 || mm_map_pages(proc,proc->krnl->mm,va,count,1)) return (addr_t)-1;
    *rg=(struct vm_rg_struct){.mode_bit=0,.rg_start=va,.rg_end=va+(addr_t)count*4096};return 0;
}
int __swap_cp_page(struct memphy_struct *src, addr_t sf, struct memphy_struct *dst, addr_t df)
{
    if (!src || !dst || sf >= (addr_t)src->maxsz/4096 || df >= (addr_t)dst->maxsz/4096) return -1;
    for (addr_t i=0;i<4096;++i) {BYTE b;if(MEMPHY_read(src,sf*4096+i,&b)||MEMPHY_write(dst,df*4096+i,b))return -1;}
    return 0;
}
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
    if (!mm) return -1;
    memset(mm,0,sizeof(*mm));
    mm->pgd=new_table(mm,4);
    mm->mmap=calloc(1,sizeof(*mm->mmap));
    if (!mm->pgd || !mm->mmap) { free(mm->pgd);free(mm->mmap);memset(mm,0,sizeof(*mm));return -1; }
    mm->mmap->vm_mm=mm;
    mm->mmap->vm_start=caller ? 0:KERNEL_BASE_DEFAULT;
    mm->mmap->vm_end=mm->mmap->sbrk=mm->mmap->vm_start;
    return 0;
}
struct vm_rg_struct *init_vm_rg(addr_t start, addr_t end)
{
    struct vm_rg_struct *r=calloc(1,sizeof(*r));
    if(r){r->rg_start=start;r->rg_end=end;r->mode_bit=1;}return r;
}
int enlist_vm_rg_node(struct vm_rg_struct **list,struct vm_rg_struct *r)
{
    if(!list||!r)return -1;
    r->rg_next=*list;*list=r;return 0;
}
int enlist_pgn_node(struct pgn_t **list,addr_t pgn)
{
    struct pgn_t *p=malloc(sizeof(*p));if(!p)return -1;p->pgn=pgn;p->pg_next=*list;*list=p;return 0;
}
static void destroy_table(addr_t *t,int level,struct memphy_struct *ram)
{
    if(!t)return;
    for(int i=0;i<512;++i) if(t[i]) {
        if(level)destroy_table((addr_t *)(uintptr_t)t[i],level-1,ram);
        else if(PAGING_PAGE_PRESENT(t[i]))MEMPHY_put_freefp(ram,PAGING_FPN(t[i]));
        else if(t[i]&PAGING_PTE_SWAPPED_MASK)free_swap_slot(swap_krnl,t[i]);
    }
    free(t);
}
void mm_destroy(struct mm_struct *mm,struct memphy_struct *ram)
{
    if(!mm)return;
    destroy_table(mm->pgd,4,ram);
    while(mm->mmap){struct vm_area_struct *v=mm->mmap;mm->mmap=v->vm_next;while(v->vm_freerg_list){struct vm_rg_struct *r=v->vm_freerg_list;v->vm_freerg_list=r->rg_next;free(r);}free(v);}
    while(mm->fifo_pgn){struct pgn_t *p=mm->fifo_pgn;mm->fifo_pgn=p->pg_next;free(p);}
    memset(mm,0,sizeof(*mm));
}
void mm_stats(struct mm_struct *mm,unsigned pid,const char *space)
{
    uint64_t n=0;for(int i=0;i<5;++i)n+=mm->tables[i];
    printf("[MM-STATS] PID=%u %s PGD=%"PRIu64" P4D=%"PRIu64" PUD=%"PRIu64" PMD=%"PRIu64" PT=%"PRIu64" bytes=%"PRIu64" peak_bytes=%"PRIu64" walks=%"PRIu64" pt_access=%"PRIu64" data_access=%"PRIu64" total=%"PRIu64" swap_in=%"PRIu64" swap_out=%"PRIu64"\n",pid,space,mm->tables[4],mm->tables[3],mm->tables[2],mm->tables[1],mm->tables[0],n*512*sizeof(addr_t),mm->peak_tables*512*sizeof(addr_t),mm->walks,mm->pt_accesses,mm->data_accesses,mm->pt_accesses+mm->data_accesses,mm->swap_ins,mm->swap_outs);
}
static void dump_table(addr_t *t,int level,addr_t prefix,addr_t start,addr_t end)
{
    for(int i=0;i<512;++i)if(t[i]){
        addr_t va=prefix|((addr_t)i<<(12+9*level));
        if(level)dump_table((addr_t *)(uintptr_t)t[i],level-1,va,start,end);
        else {if(va&(UINT64_C(1)<<56))va|=UINT64_C(0xfe00000000000000);if(va>=start&&va<=end){
            if(PAGING_PAGE_PRESENT(t[i]))printf("  VA=%016"PRIx64" PTE=%08"PRIx64" FPN=%"PRIu64"\n",va,t[i],(addr_t)PAGING_FPN(t[i]));
            else printf("  VA=%016"PRIx64" PTE=%08"PRIx64" SWAPPED SWP%"PRIu64" slot=%"PRIu64"\n",va,t[i],(addr_t)PAGING_PTE_SWPTYP(t[i]),(addr_t)PAGING_PTE_SWP(t[i]));
        }}
    }
}
int print_pgtbl(struct pcb_t *p,addr_t start,addr_t end)
{
    mm_lock();printf("[PAGE TABLE] PID=%u\n",p->pid);dump_table(p->mm->pgd,4,0,start,end);mm_stats(p->mm,p->pid,"user");mm_unlock();return 0;
}
int print_list_fp(struct framephy_struct *p){for(;p;p=p->fp_next)printf("frame=%"PRIu64"\n",p->fpn);return 0;}
int print_list_rg(struct vm_rg_struct *r){for(;r;r=r->rg_next)printf("region=%"PRIu64"..%"PRIu64"\n",r->rg_start,r->rg_end);return 0;}
int print_list_vma(struct vm_area_struct *v){for(;v;v=v->vm_next)printf("VMA=%"PRIu64"..%"PRIu64"\n",v->vm_start,v->vm_end);return 0;}
int print_list_pgn(struct pgn_t *p){for(;p;p=p->pg_next)printf("page=%"PRIu64"\n",p->pgn);return 0;}
#endif
