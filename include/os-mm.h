/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#ifndef OSMM_H
#define OSMM_H

#include <stdint.h>
#include "os-cfg.h"
#include <stddef.h>

#define MM_PAGING
#define PAGING_MAX_MMSWP 4 /* max number of supported swapped space */
#define PAGING_MAX_SYMTBL_SZ 30

/* 
 * @bksysnet: in long address mode of 64bit or original 32bit
 * the address type need to be redefined
 */

#ifdef MM64
#define ADDR_TYPE uint64_t
#else
#define ADDR_TYPE uint32_t
#endif

typedef char BYTE;
typedef ADDR_TYPE addr_t;
//typedef unsigned int uint32_t;

/* 
 * @bksysnet: the format string need to be redefined
 *            based on the address mode
 */
#ifdef MM64
#define FORMAT_ADDR "%lu"
#define FORMATX_ADDR "%16p"
#else
#define FORMAT_ADDR "%d"
#define FORMATX_ADDR "%08x"
#endif

struct pgn_t{
   addr_t pgn;
   struct pgn_t *pg_next; 
};

/*
 *  Memory region struct
 */
struct vm_rg_struct {
   int vmaid;               /* ID VMA chứa vùng. */
   unsigned long mode_bit; /* 1: user, 0: kernel; kiểm tra ở handler. */

   addr_t rg_start;         /* Địa chỉ đầu vùng (bao gồm). */
   addr_t rg_end;           /* Địa chỉ cuối vùng (không bao gồm). */

   struct vm_rg_struct *rg_next; /* Node tiếp theo trong free list. */
};

/*
 *  Memory area struct
 */
struct vm_area_struct {
   unsigned long vm_id;     /* ID VMA (bản này dùng VMA 0). */
   addr_t vm_start;         /* Đầu vùng địa chỉ ảo. */
   addr_t vm_end;           /* Giới hạn đã map theo page. */

   addr_t sbrk;             /* Break; tăng sau khi map thành công. */
/*
 * Derived field
 * unsigned long vm_limit = vm_end - vm_start
 */
   struct mm_struct *vm_mm; /* Memory manager sở hữu VMA. */
   struct vm_rg_struct *vm_freerg_list; /* Vùng byte trống, đã sắp xếp. */
   struct vm_area_struct *vm_next;
};


/* 
 * Kernel cache pool struct
 */
struct kcache_pool_struct {
   int size;
   int align;

#ifdef MM64
   addr_t storage;
#else
   uint32_t storage;
#endif
};


/* 
 * Memory management struct
 */
struct mm_struct {
#ifdef MM64
   addr_t *pgd;             /* Root duy nhất của cây bảng trang 5 cấp. */
   /* Trường legacy: cây động dùng con trỏ trong entry của bảng cha. */
   addr_t *p4d;
   addr_t *pud;
   addr_t *pmd;
   addr_t *pt;
#else
   uint32_t *pgd;
#endif

   struct vm_area_struct *mmap;
   uint64_t tables[5];      /* Số bảng PT, PMD, PUD, P4D, PGD. */
   uint64_t peak_tables;    /* Tổng số bảng lớn nhất đồng thời. */
   uint64_t walks;          /* Số lần dịch địa chỉ được tính. */
   uint64_t pt_accesses;    /* Lượt đọc entry (5 cho mỗi walk thành công). */
   uint64_t data_accesses;  /* Số byte dữ liệu được đọc/ghi theo quy ước. */

   /* Descriptor vùng nhớ cấp phát, truy cập qua register ID. */
   struct vm_rg_struct symrgtbl[PAGING_MAX_SYMTBL_SZ];

   /* FIFO phục vụ replacement; chưa dùng eviction tự động. */
   struct pgn_t *fifo_pgn;

   /* kmem cache pool */
   struct kcache_pool_struct *kcpooltbl;

};


/*
 * FRAME/MEM PHY struct
 */
struct framephy_struct { 
   addr_t fpn;              /* Số frame vật lý, có thể bằng 0. */
   struct framephy_struct *fp_next;

   /* Memory manager sở hữu frame, NULL khi trống. */
   struct mm_struct* owner;
};

struct memphy_struct {
   /* Basic field of data and size */
   BYTE *storage;           /* Mảng byte mô phỏng thiết bị vật lý. */
   int maxsz;               /* Số byte của thiết bị. */
   
   /* Sequential device fields */ 
   int rdmflg;              /* 1: random; 0: sequential. */
   int cursor;              /* Vị trí truy cập tuần tự. */

   /* Management structure */
   struct framephy_struct *free_fp_list;
   struct framephy_struct *used_fp_list;
};

#endif
