#ifndef MM64_H
#define MM64_H

#include "mm.h"
#define MM64_BITS_PER_LONG 64

#define PAGING64_CPU_BUS_WIDTH 57 /* 57-bit canonical virtual address */
#define PAGING64_PAGESZ  4096      /* 4KB or 12-bits PAGE NUMBER */

#define GENMASK64(h, l) \
	(((~0ULL) << (l)) & (~0ULL >> (MM64_BITS_PER_LONG  - (h) - 1)))

#define PAGING64_MAX_PGN (UINT64_C(1) << 45)
#define KERNEL_BASE_DEFAULT UINT64_C(0xff00000000000000)
#define MM64_ENTRIES 512
#define MM64_PAGE_SHIFT 12
#define PAGING64_PAGE_ALIGNSZ(sz) (DIV_ROUND_UP(sz,PAGING64_PAGESZ)*PAGING64_PAGESZ)


/* OFFSET */
#define PAGING64_ADDR_OFFST_HIBIT 11
#define PAGING64_ADDR_OFFST_LOBIT 0

/* PT */
#define PAGING64_ADDR_PT_HIBIT 20
#define PAGING64_ADDR_PT_LOBIT 12
#define PAGING64_ADDR_PT_SHIFT 12

/* PMD */
#define PAGING64_ADDR_PMD_HIBIT 29
#define PAGING64_ADDR_PMD_LOBIT 21

/* PUD */
#define PAGING64_ADDR_PUD_HIBIT 38
#define PAGING64_ADDR_PUD_LOBIT 30

/* P4D */
#define PAGING64_ADDR_P4D_HIBIT 47
#define PAGING64_ADDR_P4D_LOBIT 39

/* PGD */
#define PAGING64_ADDR_PGD_HIBIT 56
#define PAGING64_ADDR_PGD_LOBIT 48

/* Extract PGD Entry */
#define PAGING64_ADDR_OFFST(addr) GETVAL(addr,PAGING64_ADDR_OFFST_MASK,PAGING64_ADDR_OFFST_LOBIT)
#define PAGING64_ADDR_PT(addr)   ((addr&PAGING64_ADDR_PT_MASK)>>PAGING64_ADDR_PT_LOBIT)
//GETVAL(addr,PAGING64_ADDR_PT_MASK,PAGING64_ADDR_PT_LOBIT)
#define PAGING64_ADDR_PMD(addr)   ((addr&PAGING64_ADDR_PMD_MASK)>>PAGING64_ADDR_PMD_LOBIT)
//GETVAL(addr,PAGING64_ADDR_PMD_MASK,PAGING64_ADDR_PMD_LOBIT)
#define PAGING64_ADDR_PUD(addr)   ((addr&PAGING64_ADDR_PUD_MASK)>>PAGING64_ADDR_PUD_LOBIT)
//GETVAL(addr,PAGING64_ADDR_PUD_MASK,PAGING64_ADDR_PUD_LOBIT)
#define PAGING64_ADDR_P4D(addr)   ((addr&PAGING64_ADDR_P4D_MASK)>>PAGING64_ADDR_P4D_LOBIT)
//GETVAL(addr,PAGING64_ADDR_P4D_MASK,PAGING64_ADDR_P4D_LOBIT)
#define PAGING64_ADDR_PGD(addr)   ((addr&PAGING64_ADDR_PGD_MASK)>>PAGING64_ADDR_PGD_LOBIT)
//GETVAL(addr,PAGING64_ADDR_PGD_MASK,PAGING64_ADDR_PGD_LOBIT)


/* Masks */
#define PAGING64_ADDR_OFFST_MASK  GENMASK64(PAGING64_ADDR_OFFST_HIBIT,PAGING64_ADDR_OFFST_LOBIT)
#define PAGING64_ADDR_PT_MASK  GENMASK64(PAGING64_ADDR_PT_HIBIT,PAGING64_ADDR_PT_LOBIT)
#define PAGING64_ADDR_PMD_MASK  GENMASK64(PAGING64_ADDR_PMD_HIBIT,PAGING64_ADDR_PMD_LOBIT)
#define PAGING64_ADDR_PUD_MASK  GENMASK64(PAGING64_ADDR_PUD_HIBIT,PAGING64_ADDR_PUD_LOBIT)
#define PAGING64_ADDR_P4D_MASK  GENMASK64(PAGING64_ADDR_P4D_HIBIT,PAGING64_ADDR_P4D_LOBIT)
#define PAGING64_ADDR_PGD_MASK  GENMASK64(PAGING64_ADDR_PGD_HIBIT,PAGING64_ADDR_PGD_LOBIT)

#ifdef __cplusplus
extern "C" {
#endif

/* Interface noi bo MM64: cac thao tac cap cao giu mm_lock. */
void mm_lock(void);
void mm_unlock(void);
int mm_canonical(addr_t va);
int mm_map_pages(struct pcb_t *proc, struct mm_struct *mm, addr_t va,
                 addr_t count, int contiguous);
int mm_unmap_pages(struct pcb_t *proc, struct mm_struct *mm, addr_t va, addr_t count);
int mm_translate(struct mm_struct *mm, addr_t va, addr_t *pa, int write);

/* Page replacement (FIFO, RAM <-> active SWAP).
 * mm_swap_init: enable swapping for this kernel (call once at boot).
 * mm_translate_fault: like mm_translate, but a swapped-out page is
 *                     brought back to RAM first (page fault service). */
void mm_swap_init(struct krnl_t *kernel);
int mm_translate_fault(struct krnl_t *kernel, struct mm_struct *mm,
                       addr_t va, addr_t *pa, int write);
void MEMPHY_track(struct memphy_struct *mp, addr_t fpn,
                  struct mm_struct *owner, addr_t pgn);
int MEMPHY_pin(struct memphy_struct *mp, addr_t fpn, int delta);
struct framephy_struct *MEMPHY_pick_victim(struct memphy_struct *mp,
                                           struct mm_struct *kernel_mm);

/* DMA mapper: resolve and pin the physical runs of two regions of PID. */
struct dma_mapping;
int mm_dma_map(struct krnl_t *kernel, uint32_t pid, uint32_t src_region,
               uint32_t dst_region, uint32_t offset, uint32_t bytes,
               struct dma_mapping *mapping);

void mm_destroy(struct mm_struct *mm, struct memphy_struct *ram);
void mm_stats(struct mm_struct *mm, unsigned pid, const char *space);
int MEMPHY_get_contiguous(struct memphy_struct *mp, addr_t count, addr_t *first);
void MEMPHY_destroy(struct memphy_struct *mp);
int MEMPHY_owned(struct memphy_struct *mp, addr_t fpn, struct mm_struct *owner);
void MEMPHY_set_owner(struct memphy_struct *mp, addr_t fpn, struct mm_struct *owner);
int free_pcb_memph(struct pcb_t *caller);
int vm_insert_free(struct vm_area_struct *v, struct vm_rg_struct *r);
int mm_register(struct pcb_t *proc);
void mm_unregister(struct pcb_t *proc);
struct pcb_t *mm_lookup(struct krnl_t *kernel, uint32_t pid);

#ifdef __cplusplus
}
#endif

#endif
