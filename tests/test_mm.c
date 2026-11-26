/* Tests for Paging / syscall / USR; original input files are untouched. */
#include "mm64.h"
#include "libmem.h"
#include "syscall.h"
#include "cpu.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

static void setup_proc(struct pcb_t *p, struct krnl_t *k, unsigned id)
{
    memset(p, 0, sizeof(*p));
    p->pid = id;
    p->krnl = k;
    p->mm = calloc(1, sizeof(*p->mm));
    assert(p->mm && init_mm(p->mm, p) == 0);
    assert(mm_register(p) == 0);
}

static void destroy_proc(struct pcb_t *p)
{
    mm_unregister(p);
    assert(free_pcb_memph(p) == 0);
}

static int count_frames(struct framephy_struct *p)
{
    int n = 0;
    for (; p; p = p->fp_next) ++n;
    return n;
}

static void *worker(void *arg)
{
    struct pcb_t *p = arg;
    for (int i = 0; i < 100; ++i) {
        uint32_t val;
        assert(liballoc(p, 4100, 0) == 0);
        assert(libwrite(p, (BYTE)p->pid, 0, 4097) == 0);
        assert(libread(p, 0, 4097, &val) == 0 && val == p->pid);
        assert(libfree(p, 0) == 0);
    }
    return NULL;
}

int main(void)
{
    struct krnl_t k = {.kernel_base=KERNEL_BASE_DEFAULT};
    struct memphy_struct ram, swap;
    struct memphy_struct *swaps[4] = {&swap, NULL, NULL, NULL};
    struct mm_struct kernel;
    struct pcb_t p, q;
    assert(init_memphy(&ram, 32 * 4096, 1) == 0);
    assert(init_memphy(&swap, 4 * 4096, 0) == 0);
    k.mram = &ram;
    k.mswp = swaps;
    k.active_mswp = &swap;
    k.mm = &kernel;
    assert(init_mm(&kernel, NULL) == 0);
    setup_proc(&p, &k, 1);
    setup_proc(&q, &k, 2);

    addr_t idx[5], e, pa, pb;
    assert(get_pd_from_address(UINT64_C(0x39ffe1d9c9000),idx,idx+1,idx+2,idx+3,idx+4)==0);
    assert(idx[0]==3 && idx[1]==0x13f && idx[2]==0x1f8 && idx[3]==0xec && idx[4]==0x1c9);
    assert(mm_canonical(UINT64_C(0x00ffffffffffffff)));
    assert(mm_canonical(UINT64_C(0xff00000000000000)));
    assert(!mm_canonical(UINT64_C(0x0100000000000000)));
    assert(!mm_canonical(UINT64_C(0xfeffffffffffffff)));
    assert(PAGING64_ADDR_OFFST(UINT64_C(0x123abc)) == 0xabc);
    assert(init_pte(&e,1,0,0,0,0,0)==0 && e==PAGING_PTE_PRESENT_MASK);
    assert(init_pte(&e,1,8192,0,0,0,0)!=0);
    assert(init_pte(&e,0,0,0,1,2,123)==0 && PAGING_PTE_SWPTYP(e)==2 && PAGING_SWP(e)==123);
    puts("PASS 01 canonical address, 5-level indices, 32-bit PTE");

    assert(liballoc(&p,8193,0)==0);
    assert(p.regs[0]==0);
    assert(p.mm->tables[0]==1 && p.mm->tables[4]==1);
    assert(PAGING_FPN(pte_get_entry(p.mm,0,&p))==0);
    assert(libwrite(&p, (BYTE)0xa5,0,4095)==0);
    assert(libwrite(&p, (BYTE)0xfe,0,4096)==0);
    uint32_t val=0;
    assert(libread(&p,0,4095,&val)==0 && val==0xa5);
    uint64_t pt_before=p.mm->pt_accesses, data_before=p.mm->data_accesses;
    assert(libread(&p,0,4096,&val)==0 && val==0xfe);
    assert(p.mm->pt_accesses-pt_before==5 && p.mm->data_accesses-data_before==1);
    assert(pte_get_entry(p.mm,1,&p)&PAGING_PTE_DIRTY_MASK);
    assert(libread(&p,0,8193,&val)!=0);
    assert(libwrite(&p,1,0,UINT64_MAX)!=0);
    assert(liballoc(&p,1,0)!=0 && liballoc(&p,0,1)!=0 && liballoc(&p,1,10)!=0);
    puts("PASS 02 allocation, FPN 0, page crossing, dirty and bounds");

    assert(liballoc(&q,8193,0)==0 && q.regs[0]==p.regs[0]);
    assert(mm_translate(p.mm,4096,&pa,0)==0);
    assert(mm_translate(q.mm,4096,&pb,0)==0 && pa!=pb);
    assert(libread(&q,0,4096,&val)==0 && val==0);
    assert(libwrite(&q, 42,0,4096)==0);
    assert(libread(&p,0,4096,&val)==0 && val==0xfe);
    puts("PASS 03 process isolation: same VA, different frame");

    assert(liballoc(&p,100,1)==0);
    addr_t first=p.regs[1];
    assert(liballoc(&p,100,2)==0);
    assert(libfree(&p,1)==0 && libfree(&p,2)==0);
    assert(libfree(&p,2)!=0);
    assert(liballoc(&p,200,1)==0 && p.regs[1]==first);
    assert(libread(&p,1,0,&val)==0 && val==0);
    puts("PASS 04 free, merge, reuse, zero fill, double free rejected");

    assert(libkmem_malloc(&p,8193,3)==0);
    assert(p.regs[3]>=KERNEL_BASE_DEFAULT && p.mm->symrgtbl[3].mode_bit==0);
    assert(mm_translate(k.mm,p.regs[3],&pa,0)==0);
    assert(mm_translate(k.mm,p.regs[3]+4096,&pb,0)==0 && pb==pa+4096);
    assert(mm_translate(q.krnl->mm,p.regs[3]+8192,&pb,0)==0 && pb==pa+8192);
    assert(mm_translate(q.mm,p.regs[3],&pb,0)!=0);
    assert(libwrite(&p,3,3,0)!=0 && libread(&p,3,0,&val)!=0);
    assert(libkmem_copy_from_user(&q,0,3,0,1)!=0);
    assert(libkmem_copy_from_user(&p,0,3,4095,2)==0);
    assert(libwrite(&p,0,0,4095)==0 && libwrite(&p,0,0,4096)==0);
    assert(libkmem_copy_to_user(&p,3,0,4095,2)==0);
    assert(libread(&p,0,4095,&val)==0 && val==0xa5);
    assert(libread(&p,0,4096,&val)==0 && val==0xfe);
    assert(libkmem_copy_to_user(&p,3,0,8192,2)!=0);
    assert(libkmem_copy_from_user(&p,3,0,0,1)!=0);
    addr_t kva=p.regs[3];
    assert(libfree(&p,3)==0 && mm_translate(k.mm,kva,&pb,0)!=0);
    puts("PASS 05 contiguous kernel frames, shared kernel table, modes, copy roundtrip");

    struct sc_regs regs={0};
    assert(_syscall(&k,1,0,&regs)==0);
    assert(_syscall(&k,999,0,&regs)==-ESRCH);
    assert(_syscall(&k,1,440,&regs)==-ENOSYS);
    regs.a1=99; assert(_syscall(&k,1,17,&regs)==-EINVAL);
    regs.a1=SYSMEM_IO_READ;regs.a2=pa;
    assert(_syscall(&k,1,17,&regs)==-EPERM);
    regs.a1=SYSMEM_REGION_FREE;regs.a2=0;
    assert(_syscall(&k,1,17,&regs)==-EPERM);
    regs.a1=SYSMEM_MAP_OP;regs.a2=KERNEL_BASE_DEFAULT;regs.a3=1;
    assert(_syscall(&k,1,17,&regs)!=0);
    regs.a1=SYSMEM_MAP_OP;regs.a2=0;regs.a3=1;
    assert(_syscall(&k,1,17,&regs)==0);
    assert(libread(&p,0,4095,&val)==0 && val==0);
    addr_t oldend=p.mm->mmap->vm_end;
    regs.a2=oldend;assert(_syscall(&k,1,17,&regs)==0);
    assert(p.mm->mmap->vm_end==oldend+4096 && mm_translate(p.mm,oldend,&pb,0)==0);
    puts("PASS 06 syscall PID validation, unknown call, MAP and raw IO rejection");

    int used=count_frames(ram.used_fp_list);
    addr_t end=p.mm->mmap->vm_end;
    assert(liballoc(&p,32*4096,5)!=0);
    assert(p.mm->mmap->vm_end==end && count_frames(ram.used_fp_list)==used);
    assert(p.mm->symrgtbl[5].rg_end==0);
    assert(libkmem_malloc(&p,32*4096,5)!=0);
    assert(count_frames(ram.used_fp_list)==used);
    puts("PASS 07 out-of-RAM rollback: no frame or region leak");

    addr_t high=UINT64_C(0x39ffe1d9c9000);
    mm_lock();
    assert(mm_map_pages(&p,p.mm,high,1,0)==0);
    assert(mm_translate(p.mm,high+0xabc,&pa,0)==0 && (pa&4095)==0xabc);
    assert(mm_translate(q.mm,high,&pb,0)!=0);
    assert(p.mm->tables[4]==1 && p.mm->tables[3]==2);
    assert(mm_unmap_pages(&p,p.mm,high,1)==0 && p.mm->tables[3]==1);
    assert(mm_map_pages(&p,p.mm,UINT64_C(0x0100000000000000),1,0)!=0);
    mm_unlock();
    puts("PASS 08 high-address sparse branch, pruning, canonical rejection");

    addr_t sf;
    mm_lock();
    assert(MEMPHY_get_freefp(&swap,&sf)==0);
    MEMPHY_set_owner(&swap,sf,p.mm);
    assert(mm_translate(p.mm,4096,&pa,0)==0);
    regs=(struct sc_regs){.a1=SYSMEM_SWP_OP,.a2=pa/4096,.a3=sf,.flags=SC_INTERNAL};
    assert(_syscall(&k,p.pid,17,&regs)==0);
    BYTE byte;
    assert(MEMPHY_read(&swap,sf*4096,&byte)==0 && (unsigned char)byte==0xfe);
    assert(MEMPHY_read(&swap,swap.maxsz,&byte)!=0);
    assert(MEMPHY_put_freefp(&swap,sf)==0);
    mm_unlock();
    puts("PASS 09 swap frame copy (not automatic page replacement)");

    struct inst_t ins={.opcode=READ,.arg_0=0,.arg_1=4096,.arg_2=9};
    struct code_seg_t code={.text=&ins,.size=1};
    p.code=&code;p.pc=0;
    assert(run(&p)==0 && p.regs[9]==0xfe);
    ins.arg_2=20;p.pc=0;assert(run(&p)!=0);
    puts("PASS 10 CPU READ writes destination register; rejects register 20");

    destroy_proc(&p);destroy_proc(&q);
    assert(!ram.used_fp_list && count_frames(ram.free_fp_list)==32);
    struct pcb_t ps[4];pthread_t threads[4];
    for(int i=0;i<4;++i)setup_proc(&ps[i],&k,10+i);
    for(int i=0;i<4;++i)assert(!pthread_create(&threads[i],NULL,worker,&ps[i]));
    for(int i=0;i<4;++i)assert(!pthread_join(threads[i],NULL));
    assert(count_frames(ram.used_fp_list)==8);
    for(int i=0;i<4;++i)destroy_proc(&ps[i]);
    assert(!ram.used_fp_list && count_frames(ram.free_fp_list)==32);
    puts("PASS 11 concurrent allocations, no shared frame and complete cleanup");
    mm_destroy(&kernel,&ram);MEMPHY_destroy(&ram);MEMPHY_destroy(&swap);
    puts("ALL 11 TEST GROUPS PASSED");
    return 0;
}
