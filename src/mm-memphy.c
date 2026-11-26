/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Memory physical module mm/mm-memphy.c
 */

#include "mm64.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* The DMA engine writes RAM bytes from its own thread through the system
 * bus. CPU byte accesses take the same RAM lock (always acquired last). */
#ifndef OSSIM_NO_DMA
#include "sysbus.h"
#define RAM_LOCK()   sysbus_lock_ram()
#define RAM_UNLOCK() sysbus_unlock_ram()
#else
#define RAM_LOCK()   ((void)0)
#define RAM_UNLOCK() ((void)0)
#endif

/* Monotonic clock giving the order in which pages entered RAM. */
static uint64_t map_clock;

/* Free/used list: moi frame chi nam trong mot danh sach.
 * Caller cua cac helper noi bo giu mm_lock de thao tac nguyen tu. */
int init_memphy(struct memphy_struct *mp, addr_t size, int randomflg)
{
    if (!mp || size > INT_MAX) return -1;
    memset(mp, 0, sizeof(*mp));
    mp->maxsz = (int)size;
    mp->rdmflg = !!randomflg;
    if (!size) return 0;
    mp->storage = calloc(1, size);
    if (!mp->storage) return -1;
    struct framephy_struct **tail = &mp->free_fp_list;
    for (addr_t i = 0; i < size / PAGING64_PAGESZ; ++i) {
        *tail = calloc(1, sizeof(**tail));
        if (!*tail) { MEMPHY_destroy(mp); return -1; }
        (*tail)->fpn = i;
        tail = &(*tail)->fp_next;
    }
    return 0;
}

int MEMPHY_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
    if (!mp || !value || addr >= (addr_t)mp->maxsz) return -1;
    if (!mp->rdmflg) mp->cursor = (int)addr;
    RAM_LOCK();
    *value = mp->storage[addr];
    RAM_UNLOCK();
    return 0;
}

int MEMPHY_write(struct memphy_struct *mp, addr_t addr, BYTE data)
{
    if (!mp || addr >= (addr_t)mp->maxsz) return -1;
    if (!mp->rdmflg) mp->cursor = (int)addr;
    RAM_LOCK();
    mp->storage[addr] = data;
    RAM_UNLOCK();
    return 0;
}

int MEMPHY_get_freefp(struct memphy_struct *mp, addr_t *fpn)
{
    if (!mp || !fpn || !mp->free_fp_list) return -1;
    struct framephy_struct *fp = mp->free_fp_list;
    mp->free_fp_list = fp->fp_next;
    fp->fp_next = mp->used_fp_list;
    mp->used_fp_list = fp;
    *fpn = fp->fpn;
    memset(mp->storage + *fpn * PAGING64_PAGESZ, 0, PAGING64_PAGESZ);
    return 0;
}

int MEMPHY_put_freefp(struct memphy_struct *mp, addr_t fpn)
{
    if (!mp) return -1;
    struct framephy_struct **p = &mp->used_fp_list;
    while (*p && (*p)->fpn != fpn) p = &(*p)->fp_next;
    if (!*p) return -1; /* Khong double-free frame. */
    struct framephy_struct *fp = *p;
    if (fp->pin) return -1; /* DMA still uses this frame. */
    *p = fp->fp_next;
    fp->owner = NULL;
    fp->pgn = 0;
    fp->stamp = 0;
    p = &mp->free_fp_list;
    while (*p && (*p)->fpn < fpn) p = &(*p)->fp_next;
    fp->fp_next = *p;
    *p = fp;
    return 0;
}

/* Tim mot day frame lien tiep trong free list da sap xep. */
int MEMPHY_get_contiguous(struct memphy_struct *mp, addr_t count, addr_t *first)
{
    if (!mp || !count || !first) return -1;
    struct framephy_struct **p = &mp->free_fp_list;
    while (*p) {
        struct framephy_struct *q = *p;
        addr_t n = 1;
        while (n < count && q->fp_next && q->fp_next->fpn == q->fpn + 1) {
            q = q->fp_next; ++n;
        }
        if (n == count && q->fpn <= PAGING_PTE_FPN_MASK) {
            *first = (*p)->fpn;
            struct framephy_struct *head = *p;
            *p = q->fp_next;
            q->fp_next = mp->used_fp_list;
            mp->used_fp_list = head;
            memset(mp->storage + *first * PAGING64_PAGESZ, 0,
                   count * PAGING64_PAGESZ);
            return 0;
        }
        p = &q->fp_next;
    }
    return -1;
}

void MEMPHY_set_owner(struct memphy_struct *mp, addr_t fpn, struct mm_struct *owner)
{
    for (struct framephy_struct *p = mp->used_fp_list; p; p = p->fp_next)
        if (p->fpn == fpn) { p->owner = owner; return; }
}

int MEMPHY_owned(struct memphy_struct *mp, addr_t fpn, struct mm_struct *owner)
{
    if (!mp || !owner) return 0;
    for (struct framephy_struct *p = mp->used_fp_list; p; p = p->fp_next)
        if (p->fpn == fpn) return p->owner == owner;
    return 0;
}

/* Record which virtual page now lives in FPN; restarts its FIFO age. */
void MEMPHY_track(struct memphy_struct *mp, addr_t fpn,
                  struct mm_struct *owner, addr_t pgn)
{
    for (struct framephy_struct *p = mp->used_fp_list; p; p = p->fp_next)
        if (p->fpn == fpn) {
            p->owner = owner;
            p->pgn = pgn;
            p->stamp = ++map_clock;
            return;
        }
}

/* Add DELTA (+1 or -1) to the pin count of a used frame. */
int MEMPHY_pin(struct memphy_struct *mp, addr_t fpn, int delta)
{
    for (struct framephy_struct *p = mp ? mp->used_fp_list : NULL; p; p = p->fp_next)
        if (p->fpn == fpn) {
            if (p->pin + delta < 0) return -1;
            p->pin += delta;
            return 0;
        }
    return -1;
}

/* FIFO page replacement: the oldest user page that is not pinned.
 * Frames owned by KERNEL_MM (kernel space) are never swapped out. */
struct framephy_struct *MEMPHY_pick_victim(struct memphy_struct *mp,
                                           struct mm_struct *kernel_mm)
{
    struct framephy_struct *victim = NULL;
    for (struct framephy_struct *p = mp ? mp->used_fp_list : NULL; p; p = p->fp_next) {
        if (!p->owner || p->owner == kernel_mm || p->pin || !p->stamp)
            continue;
        if (!victim || p->stamp < victim->stamp)
            victim = p;
    }
    return victim;
}

int MEMPHY_dump(struct memphy_struct *mp)
{
    if (!mp) return -1;
    for (int i = 0; i < mp->maxsz; ++i)
        if (mp->storage[i]) printf("RAM[%x]=%02x\n", i, (unsigned char)mp->storage[i]);
    return 0;
}

void MEMPHY_destroy(struct memphy_struct *mp)
{
    if (!mp) return;
    struct framephy_struct *p, *next;
    for (p = mp->free_fp_list; p; p = next) { next = p->fp_next; free(p); }
    for (p = mp->used_fp_list; p; p = next) { next = p->fp_next; free(p); }
    free(mp->storage);
    memset(mp, 0, sizeof(*mp));
}
