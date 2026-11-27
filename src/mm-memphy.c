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

/* Free/used list: moi frame chi nam trong mot danh sach.
 * Caller cua cac helper noi bo giu memory_lock de thao tac nguyen tu. */
int MEMPHY_format(struct memphy_struct *mp, int pagesz)
{
    if(!mp || pagesz!=PAGING64_PAGESZ || mp->free_fp_list || mp->used_fp_list)return -1;
    struct framephy_struct **tail=&mp->free_fp_list;
    for(addr_t i=0;i<(addr_t)mp->maxsz/pagesz;++i){
        *tail=calloc(1,sizeof(**tail));
        if(!*tail){while(mp->free_fp_list){struct framephy_struct *f=mp->free_fp_list;mp->free_fp_list=f->fp_next;free(f);}return -1;}
        (*tail)->fpn=i;tail=&(*tail)->fp_next;
    }
    return 0;
}
int MEMPHY_mv_csr(struct memphy_struct *mp, addr_t offset)
{
    if(!mp||offset>=(addr_t)mp->maxsz)return -1;
    mp->cursor=(int)offset;return 0;
}
int MEMPHY_seq_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
    if(!mp||!value||mp->rdmflg||MEMPHY_mv_csr(mp,addr))return -1;
    *value=mp->storage[mp->cursor];return 0;
}
int MEMPHY_seq_write(struct memphy_struct *mp, addr_t addr, BYTE value)
{
    if(!mp||mp->rdmflg||MEMPHY_mv_csr(mp,addr))return -1;
    mp->storage[mp->cursor]=value;return 0;
}
int init_memphy(struct memphy_struct *mp, addr_t size, int randomflg)
{
    if(!mp||size>INT_MAX)return -1;
    memset(mp,0,sizeof(*mp));mp->maxsz=(int)size;mp->rdmflg=!!randomflg;
    if(!size)return 0;
    mp->storage=calloc(1,size);if(!mp->storage)return -1;
    if(MEMPHY_format(mp,PAGING64_PAGESZ)){free(mp->storage);mp->storage=NULL;return -1;}
    return 0;
}


int MEMPHY_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
    if (!mp || !value || addr >= (addr_t)mp->maxsz) return -1;
    if (!mp->rdmflg) return MEMPHY_seq_read(mp,addr,value);
    *value = mp->storage[addr];
    return 0;
}

int MEMPHY_write(struct memphy_struct *mp, addr_t addr, BYTE data)
{
    if (!mp || addr >= (addr_t)mp->maxsz) return -1;
    if (!mp->rdmflg) return MEMPHY_seq_write(mp,addr,data);
    mp->storage[addr] = data;
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
    *p = fp->fp_next;
    fp->owner = NULL;
    p = &mp->free_fp_list;
    while (*p && (*p)->fpn < fpn) p = &(*p)->fp_next;
    fp->fp_next = *p;
    *p = fp;
    return 0;
}

/* Tim mot day frame lien tiep trong free list da sap xep. */









int MEMPHY_dump(struct memphy_struct *mp)
{
    if (!mp) return -1;
    for (int i = 0; i < mp->maxsz; ++i)
        if (mp->storage[i]) printf("RAM[%x]=%02x\n", i, (unsigned char)mp->storage[i]);
    return 0;
}



