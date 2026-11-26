/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Example system call from section 1.6.3 of the assignment
 * ("Adding a system call to Simple Operating System"), number 440.
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "common.h"
#include "syscall.h"
#include <stdio.h>

int
__sys_xxxhandler (struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs)
{
  (void) krnl;
  (void) pid;
  printf ("The first system call parameter %d\n", (int) regs->a1);
  return 0;
}
