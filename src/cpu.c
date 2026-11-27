/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * CPU and co-processor module
 */
#include "cpu.h"
#include "mem.h"
#include "mm.h"
#include "syscall.h"
#include "libmem.h"

/* calc — Mô phỏng instruction CALC tiêu tốn một time slot, không sửa bộ nhớ.
 * Tham số: proc: PCB đang chạy.
 * Trả về: 0.
 */
int calc(struct pcb_t *proc)
{
    return ((unsigned long)proc & 0UL);
}

/* alloc — Hàm ALLOC legacy của framework, không dùng trong MM64.
 * Tham số: proc: PCB; size: số byte; reg_index: register đích.
 * Trả về: 0 nếu địa chỉ cấp phát khác 0, 1 nếu thất bại.
 */
int alloc(struct pcb_t *proc, uint32_t size, uint32_t reg_index)
{
    addr_t addr = alloc_mem(size, proc);
    if (addr == 0) {
        return 1;
    } else {
        proc->regs[reg_index] = addr;
        return 0;
    }
}

/* free_data — Hàm FREE legacy, chuyển địa chỉ trong register cho free_mem.
 * Tham số: proc: PCB; reg_index: register.
 * Trả về: Mã kết quả của free_mem.
 */
int free_data(struct pcb_t *proc, uint32_t reg_index)
{
    return free_mem(proc->regs[reg_index], proc);
}

/* read — Hàm READ legacy (không dùng trong MM64).
 * Tham số: proc: PCB; source: register nguồn; offset: độ lệch; destination: register đích.
 * Trả về: 0 hoặc 1 theo logic legacy; không đổi trong v3.
 */
int read(struct pcb_t *proc, // Process executing the instruction
uint32_t source, // Index of source register
uint32_t offset, // Source address = [source] + [offset]
uint32_t destination)
{
    // Index of destination register

    BYTE data;
    if (read_mem(proc->regs[source] + offset, proc, &data)) {
        proc->regs[destination] = data;
        return 0;
    } else {
        return 1;
    }
}

/* write — Hàm WRITE legacy (không dùng trong MM64).
 * Tham số: proc: PCB; data: byte; destination: register đích; offset: độ lệch.
 * Trả về: Mã kết quả của write_mem.
 */
int write(struct pcb_t *proc, // Process executing the instruction
BYTE data, // Data to be wrttien into memory
uint32_t destination, // Index of destination register
uint32_t offset)
{
    // Destination address =
    // [destination] + [offset]
    return write_mem(proc->regs[destination] + offset, proc, data);
}

/* run — Lấy instruction tại PC, tăng PC, kiểm tra register/kiểu đối số và gọi wrapper tương
 * ứng.
 * Tham số: proc: PCB đang chạy.
 * Trả về: 0 nếu instruction thành công; mã khác 0 nếu lỗi/hết code.
 */
int run(struct pcb_t *proc)
{
    /* Check if Program Counter point to the proper instruction */
    if (proc->pc >= proc->code->size) {
        return 1;
    }

    struct inst_t instruction = proc->code->text[proc->pc];
    proc->pc++;
    int result = 1;
#ifdef MM_PAGING
    /* CPU co 10 register. Kiem tra truoc khi ep arg_t xuong uint32_t. */
    switch (instruction.opcode) {
    case ALLOC:
    case WRITE:
        if (instruction.arg_1 >= 10) {
            return -1;
        }
        break;
    case KMALLOC:
        if (instruction.arg_1 >= 10 || instruction.arg_0 > UINT32_MAX) {
            return -1;
        }
        break;
    case FREE:
        if (instruction.arg_0 >= 10) {
            return -1;
        }
        break;
    case READ:
        if (instruction.arg_0 >= 10 || instruction.arg_2 >= 10) {
            return -1;
        }
        break;
    case COPY_FROM_USER:
    case COPY_TO_USER:
    case DMA_MEMCPY:
        if (instruction.arg_0 >= 10 || instruction.arg_1 >= 10 || instruction.arg_2 > UINT32_MAX || instruction.arg_3 > UINT32_MAX) {
            return -1;
        }
        break;
    case SYSCALL:
        if (instruction.arg_0 > UINT32_MAX) {
            return -1;
        }
        break;
    default:
        break;
    }
#endif
    switch (instruction.opcode) {
    case CALC:
        result = calc(proc);
        break;
    case ALLOC:
#ifdef MM_PAGING
        result = liballoc(proc, instruction.arg_0, instruction.arg_1);
#else
        result = alloc(proc, instruction.arg_0, instruction.arg_1);
#endif
        break;
#ifdef MM_PAGING
    case KMALLOC:
        result = libkmem_malloc(proc, instruction.arg_0, instruction.arg_1);
        break;
    case COPY_FROM_USER:
        result = libkmem_copy_from_user(proc, instruction.arg_0, instruction.arg_1, instruction.arg_2, instruction.arg_3);
        break;
    case COPY_TO_USER:
        result = libkmem_copy_to_user(proc, instruction.arg_0, instruction.arg_1, instruction.arg_2, instruction.arg_3);
        break;
    case DMA_MEMCPY:
        result = libmem_dma_memcpy(proc, instruction.arg_0, instruction.arg_1, instruction.arg_2, instruction.arg_3);
        break;
#endif
    case FREE:
#ifdef MM_PAGING
        result = libfree(proc, instruction.arg_0);
#else
        result = free_data(proc, instruction.arg_0);
#endif
        break;
    case READ:
#ifdef MM_PAGING
        {
            uint32_t data = 0;
            result = libread(proc, instruction.arg_0, instruction.arg_1, &data);
            if (!result) {
                proc->regs[instruction.arg_2] = data;
            }
        }
#else
        result = read(proc, instruction.arg_0, instruction.arg_1, instruction.arg_2);
#endif
        break;
    case WRITE:
#ifdef MM_PAGING
        result = libwrite(proc, instruction.arg_0, instruction.arg_1, instruction.arg_2);
#else
        result = write(proc, instruction.arg_0, instruction.arg_1, instruction.arg_2);
#endif
        break;
    case SYSCALL:
        result = libsyscall(proc, instruction.arg_0, instruction.arg_1, instruction.arg_2, instruction.arg_3);
        break;
    default:
        result = 1;
    }
    return result;
}
