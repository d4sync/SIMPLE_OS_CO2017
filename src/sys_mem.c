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
#include <errno.h>
#include <limits.h>

/* __sys_memmap — Handler syscall 17: lookup PID, MAP/INC/SWP/IO, region và copy user/kernel.
 * Tham số: krnl: kernel; pid: ID process; regs: thanh ghi syscall, a1 là operation.
 * Trả về: 0 nếu thành công hoặc mã lỗi âm; kết quả có thể ghi vào a3/a6. Dispatcher giữ khóa.
 */
int __sys_memmap(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs)
{
    // Bước 1: Kernel tìm PCB bằng PID. _syscall đang giữ memory_lock.
    struct pcb_t *caller = NULL;
    if (krnl != NULL) {
        caller = krnl->processes;
    }
    while (caller != NULL && caller->pid != pid) {
        caller = caller->registry_next;
    }
    if (caller == NULL) {
        return -ESRCH;
    }

    // Bước 2: regs->a1 xác định thao tác bộ nhớ của syscall số 17.
    BYTE data;
    switch (regs->a1) {
    case SYSMEM_MAP_OP:
        if (regs->a3 > INT_MAX) {
            return -EINVAL;
        }
        return vmap_pgd_memset(caller, regs->a2, (int)regs->a3);
    case SYSMEM_INC_OP:
        if (regs->a2 > INT_MAX) {
            return -EINVAL;
        }
        return inc_vma_limit(caller, (int)regs->a2, regs->a3);
    case SYSMEM_SWP_OP:
        // Chỉ cho phép wrapper nội bộ yêu cầu sao chép frame SWAP.
        if ((regs->flags & SC_INTERNAL) == 0) {
            return -EPERM;
        }
        return __mm_swap_page(caller, regs->a2, regs->a3);
    case SYSMEM_IO_READ:
    case SYSMEM_IO_WRITE: {
        // Bước 3: User không được truy cập địa chỉ vật lý trực tiếp.
        if ((regs->flags & SC_INTERNAL) == 0) {
            return -EPERM;
        }
        addr_t physical_address = regs->a2;
        addr_t frame_number = physical_address / PAGING64_PAGESZ;
        struct framephy_struct *current_frame = krnl->mram->used_fp_list;
        while (current_frame != NULL && current_frame->fpn != frame_number) {
            current_frame = current_frame->fp_next;
        }
        if (current_frame == NULL || current_frame->owner != caller->mm) {
            return -EPERM;
        }
        if (regs->a1 == SYSMEM_IO_WRITE) {
            return MEMPHY_write(krnl->mram, physical_address, (BYTE)regs->a3);
        }
        if (MEMPHY_read(krnl->mram, physical_address, &data) != 0) {
            return -EFAULT;
        }
        regs->a3 = (unsigned char)data;
        return 0;
    }
    case SYSMEM_REGION_ALLOC:
    case SYSMEM_REGION_FREE:
    case SYSMEM_REGION_READ:
    case SYSMEM_REGION_WRITE:
    case SYSMEM_KMALLOC:
    case SYSMEM_COPY_FROM:
    case SYSMEM_COPY_TO: {
        // Bước 4: Các wrapper nội bộ truyền region ID, không truyền PCB.
        if ((regs->flags & SC_INTERNAL) == 0 || regs->a2 >= 10) {
            return -EPERM;
        }
        int source_reg = (int)regs->a2;
        switch (regs->a1) {
        case SYSMEM_REGION_ALLOC:
            return __alloc(caller, 0, source_reg, regs->a3, &regs->a6);
        case SYSMEM_REGION_FREE:
            return __free(caller, 0, source_reg);
        case SYSMEM_REGION_READ:
            if (__read(caller, 0, source_reg, regs->a3, &data) != 0) {
                return -EFAULT;
            }
            regs->a6 = (unsigned char)data;
            return 0;
        case SYSMEM_REGION_WRITE:
            return __write(caller, 0, source_reg, regs->a3, (BYTE)regs->a4);
        case SYSMEM_KMALLOC:
            if (__kmalloc(caller, 0, source_reg, regs->a3, &regs->a6) != 0) {
                return -ENOMEM;
            }
            return 0;
        default: {
            // Bước 5: COPY_FROM_USER hoặc COPY_TO_USER.
            // Kiểm tra cả hai vùng và toàn bộ khoảng sao chép trước khi ghi.
            if (regs->a3 >= 10) {
                return -EINVAL;
            }
            int destination_reg = (int)regs->a3;
            struct vm_rg_struct *source = get_symrg_byid(caller->mm, source_reg);
            struct vm_rg_struct *destination = get_symrg_byid(caller->mm, destination_reg);
            if (source == NULL || destination == NULL) {
                return -EINVAL;
            }
            if (source->rg_end <= source->rg_start ||
                destination->rg_end <= destination->rg_start) {
                return -EINVAL;
            }
            unsigned source_mode = (regs->a1 == SYSMEM_COPY_FROM);
            if (source->mode_bit != source_mode || destination->mode_bit == source_mode ||
                source->vmaid != 0 || destination->vmaid != 0) {
                return -EINVAL;
            }
            addr_t offset = regs->a4;
            addr_t size = regs->a5;
            addr_t source_size = source->rg_end - source->rg_start;
            addr_t destination_size = destination->rg_end - destination->rg_start;
            // Trừ trước khi so sánh size để tránh overflow của offset + size.
            if (offset > source_size || offset > destination_size) {
                return -EINVAL;
            }
            if (size > source_size - offset || size > destination_size - offset) {
                return -EINVAL;
            }
            for (addr_t i = 0; i < size; ++i) {
                addr_t current_offset = offset + i;
                BYTE byte;
                int result;
                if (source_mode == 1) {
                    result = __read_user_mem(caller, 0, source_reg, current_offset, &byte);
                } else {
                    result = __read_kernel_mem(caller, 0, source_reg, current_offset, &byte);
                }
                if (result != 0) {
                    return result;
                }
                if (source_mode == 1) {
                    result = __write_kernel_mem(caller, 0, destination_reg, current_offset, byte);
                } else {
                    result = __write_user_mem(caller, 0, destination_reg, current_offset, byte);
                }
                if (result != 0) {
                    return result;
                }
            }
            return 0;
        }
        }
    }
    default:
        return -EINVAL;
    }
}
