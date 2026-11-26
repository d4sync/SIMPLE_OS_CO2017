/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include "dma.h"
#include <errno.h>
#include <limits.h>

#define REG_COUNT 10

/* Region operations issued by the trusted libmem wrappers.  */
static int
memmap_region_op (struct pcb_t *p, struct sc_regs *r)
{
  BYTE b;

  if (!(r->flags & SC_INTERNAL) || r->a2 >= REG_COUNT)
    return -EPERM;

  switch (r->a1)
    {
    case SYSMEM_REGION_ALLOC:
      return __alloc (p, 0, (int) r->a2, r->a3, &r->a6);
    case SYSMEM_REGION_FREE:
      return __free (p, 0, (int) r->a2);
    case SYSMEM_REGION_READ:
      if (__read (p, 0, (int) r->a2, r->a3, &b))
        return -EFAULT;
      r->a6 = (unsigned char) b;
      return 0;
    case SYSMEM_REGION_WRITE:
      return __write (p, 0, (int) r->a2, r->a3, (BYTE) r->a4);
    case SYSMEM_KMALLOC:
      return __kmalloc (p, 0, (int) r->a2, r->a3, &r->a6) == 0 ? 0 : -ENOMEM;
    case SYSMEM_COPY_FROM:
    case SYSMEM_COPY_TO:
      if (r->a3 >= REG_COUNT)
        return -EINVAL;
      return mm_copy_region (p, (uint32_t) r->a2, (uint32_t) r->a3, r->a4,
                             r->a5, r->a1 == SYSMEM_COPY_FROM);
    default:
      return -EINVAL;
    }
}

/* Hand a region-to-region copy to the DMA co-processor (asynchronous).  */
static int
memmap_dma_op (struct krnl_t *krnl, uint32_t pid, struct sc_regs *r)
{
  if (!(r->flags & SC_INTERNAL))
    return -EPERM;
  if (r->a2 >= REG_COUNT || r->a3 >= REG_COUNT
      || r->a4 > UINT32_MAX || r->a5 > UINT32_MAX)
    return -EINVAL;
#ifdef OSSIM_NO_DMA
  (void) krnl;
  (void) pid;
  return -ENOSYS;
#else
  return dma_copy_regions (krnl, pid, (uint32_t) r->a2, (uint32_t) r->a3,
                           (uint32_t) r->a4, (uint32_t) r->a5);
#endif
}

/*
 * memmap - syscall 17
 *   a1 = operation, a2/a3/... = operands.
 * Only the PID crosses the user/kernel boundary; the PCB is looked up here.
 */
int
__sys_memmap (struct krnl_t *krnl, uint32_t pid, struct sc_regs *r)
{
  struct pcb_t *p = mm_lookup (krnl, pid);
  BYTE b;

  if (!p)
    return -ESRCH;

  switch (r->a1)
    {
    case SYSMEM_MAP_OP:
      if (r->a3 > INT_MAX)
        return -EINVAL;
      return vmap_pgd_memset (p, r->a2, (int) r->a3);

    case SYSMEM_INC_OP:
      if (r->a2 > INT_MAX)
        return -EINVAL;
      return inc_vma_limit (p, (int) r->a2, r->a3);

    case SYSMEM_SWP_OP:
      if (!(r->flags & SC_INTERNAL))
        return -EPERM;
      return __mm_swap_page (p, r->a2, r->a3);

    case SYSMEM_IO_READ:
    case SYSMEM_IO_WRITE:
      /* A user "syscall 17 4 ..." must not touch raw physical memory:
         only the wrappers (which translated a checked region) may.  */
      if (!(r->flags & SC_INTERNAL)
          || !MEMPHY_owned (krnl->mram, r->a2 / 4096, p->mm))
        return -EPERM;
      if (r->a1 == SYSMEM_IO_WRITE)
        return MEMPHY_write (krnl->mram, r->a2, (BYTE) r->a3);
      if (MEMPHY_read (krnl->mram, r->a2, &b))
        return -EFAULT;
      r->a3 = (unsigned char) b;
      return 0;

    case SYSMEM_REGION_ALLOC:
    case SYSMEM_REGION_FREE:
    case SYSMEM_REGION_READ:
    case SYSMEM_REGION_WRITE:
    case SYSMEM_KMALLOC:
    case SYSMEM_COPY_FROM:
    case SYSMEM_COPY_TO:
      return memmap_region_op (p, r);

    case SYSMEM_DMA_OP:
      return memmap_dma_op (krnl, pid, r);

    default:
      return -EINVAL;
    }
}
