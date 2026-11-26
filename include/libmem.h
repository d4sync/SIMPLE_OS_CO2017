/*
 * Copyright (C) 2025 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Sierra release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#ifndef LIBMEM_H
#define LIBMEM_H
#include "common.h"

#define SYSMEM_MAP_OP 1
#define SYSMEM_INC_OP 2
#define SYSMEM_SWP_OP 3
#define SYSMEM_IO_READ 4
#define SYSMEM_IO_WRITE 5
/* Noi bo: giu nguyen 5 operation cong khai cua tai lieu. */
#define SYSMEM_REGION_ALLOC 6
#define SYSMEM_REGION_FREE 7
#define SYSMEM_REGION_READ 8
#define SYSMEM_REGION_WRITE 9
#define SYSMEM_KMALLOC 10
#define SYSMEM_COPY_FROM 11
#define SYSMEM_COPY_TO 12
int mm_copy_region(struct pcb_t*, uint32_t, uint32_t, addr_t, addr_t, unsigned);

extern struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid);
int liballoc(struct pcb_t *, addr_t, uint32_t);
int libfree(struct pcb_t *, uint32_t);
int libread(struct pcb_t*, uint32_t, addr_t, uint32_t*);
int libwrite(struct pcb_t*, BYTE, uint32_t, addr_t);
int libkmem_malloc(struct pcb_t*, addr_t, uint32_t);
int libkmem_copy_from_user(struct pcb_t*, uint32_t, uint32_t, addr_t, addr_t);
int libkmem_copy_to_user(struct pcb_t*, uint32_t, uint32_t, addr_t, addr_t);
int libmem_dma_memcpy(struct pcb_t*, uint32_t, uint32_t, addr_t, addr_t);
#endif
