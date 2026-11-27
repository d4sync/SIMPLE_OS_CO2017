/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * System Library
 * Memory Module Library libmem.c
 */
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <limits.h>
#define REG_COUNT 10

/* get_symrg_byid — Tìm descriptor vùng nhớ theo register ID 0..9.
 * Tham số: mm: memory manager user; id: số register.
 * Trả về: Con trỏ symbol region hoặc NULL nếu ID không hợp lệ.
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int id)
{
    if (mm == NULL || id < 0 || id >= REG_COUNT) {
        return NULL;
    }
    return &mm->symrgtbl[id];
}
/* enlist_vm_freerg_list — Chèn region vào free list đã sắp xếp và gộp vùng kề nhau.
 * Tham số: mm: memory manager; region: node được chuyển quyền sở hữu vào danh sách.
 * Trả về: 0 nếu thành công, -1 nếu region/VMA lỗi.
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *region)
{
    if (region == NULL || region->rg_start >= region->rg_end) {
        return -1;
    }
    struct vm_area_struct *vma = get_vma_by_num(mm, region->vmaid);
    if (vma == NULL) {
        return -1;
    }
    // Bước 1: Tìm vị trí chèn region theo địa chỉ tăng dần.
    struct vm_rg_struct *previous = NULL;
    struct vm_rg_struct *current = vma->vm_freerg_list;
    while (current != NULL && current->rg_start < region->rg_start) {
        previous = current;
        current = current->rg_next;
    }
    region->rg_next = current;
    if (previous == NULL) {
        vma->vm_freerg_list = region;
    } else {
        previous->rg_next = region;
    }
    // Bước 2: Gộp các vùng liền kề hoặc chồng lấn (coalescing).
    current = vma->vm_freerg_list;
    while (current != NULL && current->rg_next != NULL) {
        struct vm_rg_struct *next_region = current->rg_next;
        if (current->rg_end >= next_region->rg_start) {
            if (next_region->rg_end > current->rg_end) {
                current->rg_end = next_region->rg_end;
            }
            current->rg_next = next_region->rg_next;
            free(next_region);
        } else {
            current = next_region;
        }
    }
    return 0;
}

/* get_free_vmrg_area — First-fit: lấy một khoảng từ free list và thu nhỏ hoặc xóa node cũ.
 * Tham số: caller: PCB; id: VMA; size: số byte; region: descriptor đầu ra.
 * Trả về: 0 nếu tìm thấy, -1 nếu không đủ vùng trống/lỗi.
 */
int get_free_vmrg_area(struct pcb_t *caller, int id, int size, struct vm_rg_struct *region)
{
    struct vm_area_struct *vma = get_vma_by_num(caller->mm, id);
    if (vma == NULL || size <= 0 || region == NULL) {
        return -1;
    }
    // Bước 1: First-fit: tìm vùng trống đầu tiên đủ số byte yêu cầu.
    struct vm_rg_struct *previous = NULL;
    struct vm_rg_struct *current = vma->vm_freerg_list;
    while (current != NULL) {
        addr_t available_size = current->rg_end - current->rg_start;
        if ((addr_t)size <= available_size) {
            // Bước 2: Tách phần đầu của vùng trống để cấp phát.
            *region = (struct vm_rg_struct){0};
            region->vmaid = id;
            region->mode_bit = 1;
            region->rg_start = current->rg_start;
            region->rg_end = current->rg_start + size;
            current->rg_start += size;
            // Nếu đã lấy hết region, bỏ node khỏi free list.
            if (current->rg_start == current->rg_end) {
                if (previous == NULL) {
                    vma->vm_freerg_list = current->rg_next;
                } else {
                    previous->rg_next = current->rg_next;
                }
                free(current);
            }
            return 0;
        }
        previous = current;
        current = current->rg_next;
    }
    return -1;
}
/* Wrapper user chỉ gửi PID và các đối số trong thanh ghi qua syscall. */

/* __alloc — Cấp phát vùng user, mở rộng VMA nếu cần, zero-fill và lưu VA vào register.
 * Tham số: caller: PCB đã lookup; id: VMA; reg: register; size: số byte; out: nơi nhận VA.
 * Trả về: 0 nếu thành công, -1 nếu lỗi. Tự khóa memory_lock, hỗ trợ gọi lồng.
 */
int __alloc(struct pcb_t *caller, int id, int reg, addr_t size, addr_t *out)
{
    int result = -1;
    pthread_mutex_lock(&memory_lock);
    // Bước 1: Kiểm tra register, VMA và kích thước.
    struct vm_rg_struct *symbol_region = get_symrg_byid(caller->mm, reg);
    struct vm_rg_struct allocated_region;
    struct vm_area_struct *vma = get_vma_by_num(caller->mm, id);
    if (symbol_region == NULL || vma == NULL || out == NULL || size == 0 || size > INT_MAX) {
        goto done;
    }
    if (symbol_region->rg_end > symbol_region->rg_start) {
        goto done;
    }
    if (get_free_vmrg_area(caller, id, (int)size, &allocated_region)) {
        // Bước 2: Nếu First-fit thất bại, chỉ mở rộng VMA phần còn thiếu.
        addr_t missing = size;
        for (struct vm_rg_struct *free_region = vma->vm_freerg_list; free_region; free_region = free_region->rg_next) {
            if (free_region->rg_end == vma->vm_end && free_region->rg_end - free_region->rg_start < size) {
                missing -= free_region->rg_end - free_region->rg_start;
            }
        }
        struct sc_regs request = {0};
        request.a1 = SYSMEM_INC_OP;
        request.a2 = id;
        request.a3 = missing;
        request.flags = SC_INTERNAL;
        if (_syscall(caller->krnl, caller->pid, 17, &request) || get_free_vmrg_area(caller, id, (int)size, &allocated_region)) {
            goto done;
        }
    }
    // Bước 3: Zero-fill theo từng đoạn trong page để không lộ dữ liệu cũ.
    for (addr_t virtual_address = allocated_region.rg_start; virtual_address < allocated_region.rg_end;) {
        addr_t physical_address;
        addr_t pte = pte_get_entry(caller->mm, virtual_address >> 12, caller);
        if (!PAGING_PAGE_PRESENT(pte) || (pte&PAGING_PTE_SWAPPED_MASK)) {
            goto done;
        }
        addr_t frame_number = PAGING_FPN(pte);
        addr_t page_offset = virtual_address & 4095;
        physical_address = frame_number * PAGING64_PAGESZ + page_offset;
        ++caller->mm->walks;
        caller->mm->pt_accesses += 5;
        addr_t chunk_size = PAGING64_PAGESZ - page_offset;
        if (chunk_size > allocated_region.rg_end - virtual_address) {
            chunk_size = allocated_region.rg_end - virtual_address;
        }
        memset(caller->krnl->mram->storage + physical_address, 0, chunk_size);
        virtual_address += chunk_size;
        caller->mm->data_accesses += chunk_size;
    }
    // Bước 4: Lưu descriptor và VA vào register sau khi thành công.
    *symbol_region = allocated_region;
    *out = allocated_region.rg_start;
    caller->regs[reg] = allocated_region.rg_start;
    result = 0;
#ifdef IODUMP
    printf("[REGION] PID=%u reg=%d mode=user range=[%016" PRIx64 ",%016" PRIx64 ") sbrk=%016" PRIx64 "\n", caller->pid, reg, allocated_region.rg_start, allocated_region.rg_end, vma->sbrk);
    print_list_rg(vma->vm_freerg_list);
#endif

done:
    pthread_mutex_unlock(&memory_lock);
    return result;
}
/* FREE user trả region về free list; frame giữ lại để tái sử dụng.
 * FREE kernel bỏ mapping và trả frame ngay. */
/* __free — Giải phóng symbol: user trả free region, kernel trả frame và xóa PTE.
 * Tham số: caller: PCB; id: VMA; reg: register của region.
 * Trả về: 0 nếu thành công, -1 nếu lỗi hoặc double-free.
 */
int __free(struct pcb_t *caller, int id, int reg)
{
    int result = -1;
    pthread_mutex_lock(&memory_lock);
    struct vm_rg_struct *symbol_region = get_symrg_byid(caller->mm, reg);
    if (!(symbol_region && symbol_region->rg_end > symbol_region->rg_start) || symbol_region->vmaid != id) {
        goto done;
    }
    if (symbol_region->mode_bit) {
        struct vm_rg_struct *region = malloc(sizeof(*region));
        if (!region) {
            goto done;
        }
        *region = *symbol_region;
        region->rg_next = NULL;
        if (enlist_vm_freerg_list(caller->mm, region)) {
            free(region);
            goto done;
        }
    } else {
        addr_t page_count = (symbol_region->rg_end - symbol_region->rg_start + 4095) / 4096;
        for (addr_t i = 0; i < page_count; ++i) {
            addr_t page_number = (symbol_region->rg_start >> 12) + i, pte = pte_get_entry(caller->krnl->mm, page_number, caller);
            if (PAGING_PAGE_PRESENT(pte)) {
                MEMPHY_put_freefp(caller->krnl->mram, PAGING_FPN(pte));
            }
            pte_set_entry(caller->krnl->mm, page_number, 0, caller);
        }
    }
#ifdef IODUMP
    printf("[FREE] PID=%u reg=%d mode=%lu range=[%016" PRIx64 ",%016" PRIx64 ")\n", caller->pid, reg, symbol_region->mode_bit, symbol_region->rg_start, symbol_region->rg_end);
#endif
    memset(symbol_region, 0, sizeof(*symbol_region));
    caller->regs[reg] = 0;
    result = 0;
#ifdef IODUMP
    print_list_rg(caller->mm->mmap->vm_freerg_list);
#endif

done:
    pthread_mutex_unlock(&memory_lock);
    return result;
}
/* Mỗi lần đọc/ghi: dịch địa chỉ, kiểm tra quyền, sau đó truy cập RAM. */
/* Prototype int gốc chỉ nhận địa chỉ không âm trong giới hạn int. */
/* pg_getpage — Lấy FPN từ PTE present của trang, theo prototype int gốc.
 * Tham số: mm: bảng trang; pgn: số trang không âm; fpn: đầu ra; caller: PCB.
 * Trả về: 0 nếu thành công, -1 nếu trang lỗi/chưa resident.
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
    if (pgn < 0 || !fpn) {
        return -1;
    }
    addr_t pte = pte_get_entry(mm, (addr_t)pgn, caller);
    if (!PAGING_PAGE_PRESENT(pte) || (pte&PAGING_PTE_SWAPPED_MASK)) {
        return -1;
    }
    *fpn = (int)PAGING_FPN(pte);
    return 0;
}
/* pg_getval — Đọc byte tại VA kiểu int, kiểm tra owner và cập nhật thống kê.
 * Tham số: mm: bảng trang; addr: VA không âm; data: đầu ra; caller: PCB.
 * Trả về: 0 nếu thành công, -1 nếu lỗi. Caller giữ khóa nếu chạy đồng thời.
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
    int fpn;
    if (addr < 0 || !data || pg_getpage(mm, addr >> 12, &fpn, caller)) {
        return -1;
    }
    struct framephy_struct *current_frame = caller->krnl->mram->used_fp_list;
    while (current_frame && current_frame->fpn != (addr_t)fpn) {
        current_frame = current_frame->fp_next;
    }
    if (!current_frame || current_frame->owner != mm) {
        return -1;
    }
    ++mm->walks;
    mm->pt_accesses += 5;
    int result = MEMPHY_read(caller->krnl->mram, (addr_t)fpn*4096 + (addr&4095), data);
    if (!result) {
        ++mm->data_accesses;
    }
    return result;
}
/* pg_setval — Ghi byte tại VA kiểu int, kiểm tra owner và đặt dirty bit.
 * Tham số: mm: bảng trang; addr: VA không âm; value: byte; caller: PCB.
 * Trả về: 0 nếu thành công, -1 nếu lỗi. Caller giữ khóa nếu chạy đồng thời.
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
    int fpn;
    if (addr < 0 || pg_getpage(mm, addr >> 12, &fpn, caller)) {
        return -1;
    }
    struct framephy_struct *current_frame = caller->krnl->mram->used_fp_list;
    while (current_frame && current_frame->fpn != (addr_t)fpn) {
        current_frame = current_frame->fp_next;
    }
    if (!current_frame || current_frame->owner != mm) {
        return -1;
    }
    ++mm->walks;
    mm->pt_accesses += 5;
    int result = MEMPHY_write(caller->krnl->mram, (addr_t)fpn*4096 + (addr&4095), value);
    if (!result) {
        ++mm->data_accesses;
        addr_t pte = pte_get_entry(mm, (addr_t)addr >> 12, caller);
        pte_set_entry(mm, (addr_t)addr >> 12, pte | PAGING_PTE_DIRTY_MASK, caller);
    }
    return result;
}
/* __read — Đọc một byte từ region user bằng VA 64 bit rồi gửi syscall IO.
 * Tham số: caller: PCB; id: VMA; reg: region; offset: độ lệch byte; data: đầu ra.
 * Trả về: 0 nếu thành công, mã lỗi âm nếu sai quyền/phạm vi/mapping.
 */
int __read(struct pcb_t *caller, int id, int reg, addr_t offset, BYTE *data)
{
    pthread_mutex_lock(&memory_lock);
    int result = -1;
    // Bước 1: Kiểm tra region, mode và offset trước khi dịch địa chỉ.
    struct vm_rg_struct *region = get_symrg_byid(caller->mm, reg);
    if (region == NULL || region->rg_end <= region->rg_start || region->vmaid != id || region->mode_bit != 1) {
        goto done;
    }
    addr_t region_size = region->rg_end - region->rg_start;
    if (offset >= region_size || data == NULL) {
        goto done;
    }
    // Bước 2: Chọn bảng trang user riêng của process, lấy PTE.
    struct mm_struct *mm = caller->mm;
    addr_t virtual_address = region->rg_start + offset;
    addr_t page_number = virtual_address >> 12;
    addr_t pte = pte_get_entry(mm, page_number, caller);
    if (!PAGING_PAGE_PRESENT(pte) || (pte & PAGING_PTE_SWAPPED_MASK)) {
        goto done;
    }
    // Bước 3: Kiểm tra frame thật sự thuộc memory manager này.
    addr_t frame_number = PAGING_FPN(pte);
    struct framephy_struct *current_frame = caller->krnl->mram->used_fp_list;
    while (current_frame != NULL && current_frame->fpn != frame_number) {
        current_frame = current_frame->fp_next;
    }
    if (current_frame == NULL || current_frame->owner != mm) {
        goto done;
    }
    // Bước 4: PA = FPN * page size + offset 12 bit thấp.
    addr_t page_offset = virtual_address & 4095;
    addr_t physical_address = frame_number * PAGING64_PAGESZ + page_offset;
    ++mm->walks;
    mm->pt_accesses += 5;
    // Bước 5: Gửi syscall IO nội bộ theo PID và cập nhật thống kê.
    struct sc_regs request = {0};
    request.a1 = SYSMEM_IO_READ;
    request.a2 = physical_address;
    request.flags = SC_INTERNAL;
    result = _syscall(caller->krnl, caller->pid, 17, &request);
    if (result == 0) {
        *data = (BYTE)request.a3;
    }
    if (result == 0) {
        ++mm->data_accesses;
    }
done:
    // Mọi đường thoát sau khi lock đều đi qua đây.
    pthread_mutex_unlock(&memory_lock);
    return result;
}

/* __write — Ghi một byte vào region user qua syscall IO và đặt dirty bit.
 * Tham số: caller: PCB; id: VMA; reg: region; offset: độ lệch; data: byte.
 * Trả về: 0 nếu thành công, mã lỗi âm nếu lỗi.
 */
int __write(struct pcb_t *caller, int id, int reg, addr_t offset, BYTE data)
{
    pthread_mutex_lock(&memory_lock);
    int result = -1;
    // Bước 1: Kiểm tra region, mode và offset trước khi dịch địa chỉ.
    struct vm_rg_struct *region = get_symrg_byid(caller->mm, reg);
    if (region == NULL || region->rg_end <= region->rg_start || region->vmaid != id || region->mode_bit != 1) {
        goto done;
    }
    addr_t region_size = region->rg_end - region->rg_start;
    if (offset >= region_size) {
        goto done;
    }
    // Bước 2: Chọn bảng trang user riêng của process, lấy PTE.
    struct mm_struct *mm = caller->mm;
    addr_t virtual_address = region->rg_start + offset;
    addr_t page_number = virtual_address >> 12;
    addr_t pte = pte_get_entry(mm, page_number, caller);
    if (!PAGING_PAGE_PRESENT(pte) || (pte & PAGING_PTE_SWAPPED_MASK)) {
        goto done;
    }
    // Bước 3: Kiểm tra frame thật sự thuộc memory manager này.
    addr_t frame_number = PAGING_FPN(pte);
    struct framephy_struct *current_frame = caller->krnl->mram->used_fp_list;
    while (current_frame != NULL && current_frame->fpn != frame_number) {
        current_frame = current_frame->fp_next;
    }
    if (current_frame == NULL || current_frame->owner != mm) {
        goto done;
    }
    // Bước 4: PA = FPN * page size + offset 12 bit thấp.
    addr_t page_offset = virtual_address & 4095;
    addr_t physical_address = frame_number * PAGING64_PAGESZ + page_offset;
    ++mm->walks;
    mm->pt_accesses += 5;
    // Bước 5: Gửi syscall IO nội bộ theo PID và cập nhật thống kê.
    struct sc_regs request = {0};
    request.a1 = SYSMEM_IO_WRITE;
    request.a2 = physical_address;
    request.flags = SC_INTERNAL;
    request.a3 = (unsigned char)data;
    result = _syscall(caller->krnl, caller->pid, 17, &request);
    if (result == 0) {
        ++mm->data_accesses;
        pte_set_entry(mm, page_number, pte | PAGING_PTE_DIRTY_MASK, caller);
    }
done:
    // Mọi đường thoát sau khi lock đều đi qua đây.
    pthread_mutex_unlock(&memory_lock);
    return result;
}

/* __read_user_mem — Gọi đường đọc user có kiểm tra quyền, dùng trong COPY.
 * Tham số: caller: PCB; id: VMA; reg: region; offset: độ lệch; data: đầu ra.
 * Trả về: Kết quả của __read (0 hoặc mã lỗi âm).
 */
int __read_user_mem(struct pcb_t *caller, int id, int reg, addr_t offset, BYTE *data)
{
    return __read(caller, id, reg, offset, data);
}
/* __write_user_mem — Gọi đường ghi user có kiểm tra quyền, dùng trong COPY.
 * Tham số: caller: PCB; id: VMA; reg: region; offset: độ lệch; data: byte.
 * Trả về: Kết quả của __write (0 hoặc mã lỗi âm).
 */
int __write_user_mem(struct pcb_t *caller, int id, int reg, addr_t offset, BYTE data)
{
    return __write(caller, id, reg, offset, data);
}
/* __read_kernel_mem — Đọc byte từ region kernel do process sở hữu bằng bảng trang kernel.
 * Tham số: caller: PCB; id: VMA; reg: region; offset: độ lệch; data: đầu ra.
 * Trả về: 0 nếu thành công, -1 nếu lỗi.
 */
int __read_kernel_mem(struct pcb_t *caller, int id, int reg, addr_t offset, BYTE *data)
{
    pthread_mutex_lock(&memory_lock);
    int result = -1;
    // Bước 1: Kiểm tra region, mode và offset trước khi dịch địa chỉ.
    struct vm_rg_struct *region = get_symrg_byid(caller->mm, reg);
    if (region == NULL || region->rg_end <= region->rg_start || region->vmaid != id || region->mode_bit != 0) {
        goto done;
    }
    addr_t region_size = region->rg_end - region->rg_start;
    if (offset >= region_size || data == NULL) {
        goto done;
    }
    // Bước 2: Chọn bảng trang kernel dùng chung, lấy PTE.
    struct mm_struct *mm = caller->krnl->mm;
    addr_t virtual_address = region->rg_start + offset;
    addr_t page_number = virtual_address >> 12;
    addr_t pte = pte_get_entry(mm, page_number, caller);
    if (!PAGING_PAGE_PRESENT(pte) || (pte & PAGING_PTE_SWAPPED_MASK)) {
        goto done;
    }
    // Bước 3: Kiểm tra frame thật sự thuộc memory manager này.
    addr_t frame_number = PAGING_FPN(pte);
    struct framephy_struct *current_frame = caller->krnl->mram->used_fp_list;
    while (current_frame != NULL && current_frame->fpn != frame_number) {
        current_frame = current_frame->fp_next;
    }
    if (current_frame == NULL || current_frame->owner != mm) {
        goto done;
    }
    // Bước 4: PA = FPN * page size + offset 12 bit thấp.
    addr_t page_offset = virtual_address & 4095;
    addr_t physical_address = frame_number * PAGING64_PAGESZ + page_offset;
    ++mm->walks;
    mm->pt_accesses += 5;
    // Bước 5: Truy cập RAM trong kernel và cập nhật thống kê.
    result = MEMPHY_read(caller->krnl->mram, physical_address, data);
    if (result == 0) {
        ++mm->data_accesses;
    }
done:
    // Mọi đường thoát sau khi lock đều đi qua đây.
    pthread_mutex_unlock(&memory_lock);
    return result;
}

/* __write_kernel_mem — Ghi byte vào region kernel, dùng bảng trang chung và đặt dirty bit.
 * Tham số: caller: PCB; id: VMA; reg: region; offset: độ lệch; data: byte.
 * Trả về: 0 nếu thành công, -1 nếu lỗi.
 */
int __write_kernel_mem(struct pcb_t *caller, int id, int reg, addr_t offset, BYTE data)
{
    pthread_mutex_lock(&memory_lock);
    int result = -1;
    // Bước 1: Kiểm tra region, mode và offset trước khi dịch địa chỉ.
    struct vm_rg_struct *region = get_symrg_byid(caller->mm, reg);
    if (region == NULL || region->rg_end <= region->rg_start || region->vmaid != id || region->mode_bit != 0) {
        goto done;
    }
    addr_t region_size = region->rg_end - region->rg_start;
    if (offset >= region_size) {
        goto done;
    }
    // Bước 2: Chọn bảng trang kernel dùng chung, lấy PTE.
    struct mm_struct *mm = caller->krnl->mm;
    addr_t virtual_address = region->rg_start + offset;
    addr_t page_number = virtual_address >> 12;
    addr_t pte = pte_get_entry(mm, page_number, caller);
    if (!PAGING_PAGE_PRESENT(pte) || (pte & PAGING_PTE_SWAPPED_MASK)) {
        goto done;
    }
    // Bước 3: Kiểm tra frame thật sự thuộc memory manager này.
    addr_t frame_number = PAGING_FPN(pte);
    struct framephy_struct *current_frame = caller->krnl->mram->used_fp_list;
    while (current_frame != NULL && current_frame->fpn != frame_number) {
        current_frame = current_frame->fp_next;
    }
    if (current_frame == NULL || current_frame->owner != mm) {
        goto done;
    }
    // Bước 4: PA = FPN * page size + offset 12 bit thấp.
    addr_t page_offset = virtual_address & 4095;
    addr_t physical_address = frame_number * PAGING64_PAGESZ + page_offset;
    ++mm->walks;
    mm->pt_accesses += 5;
    // Bước 5: Truy cập RAM trong kernel và cập nhật thống kê.
    result = MEMPHY_write(caller->krnl->mram, physical_address, data);
    if (result == 0) {
        ++mm->data_accesses;
        pte_set_entry(mm, page_number, pte | PAGING_PTE_DIRTY_MASK, caller);
    }
done:
    // Mọi đường thoát sau khi lock đều đi qua đây.
    pthread_mutex_unlock(&memory_lock);
    return result;
}
/* Public wrappers: khong dua PCB pointer vao kernel dispatcher.
 * Chi cac helper __alloc/__read/... da duoc handler goi moi truy cap MM. */

/* liballoc — Wrapper instruction ALLOC: đóng gói size/register và gửi syscall theo PID.
 * Tham số: caller: PCB ở user; size: số byte; reg: register đích.
 * Trả về: 0 nếu thành công, mã lỗi của syscall nếu thất bại.
 */
int liballoc(struct pcb_t *caller, addr_t size, uint32_t reg)
{
    struct sc_regs request = {0};
    request.a1 = SYSMEM_REGION_ALLOC;
    request.a2 = reg;
    request.a3 = size;
    request.flags = SC_INTERNAL;
    int result = _syscall(caller->krnl, caller->pid, 17, &request);
    if (!result) {
        printf("[ALLOC] PID=%u reg=%u VA=%016" PRIx64 " size=%" PRIu64 "\n", caller->pid, reg, request.a6, size);
    }
    return result;
}

/* libfree — Wrapper FREE: gửi register chứa region cần giải phóng.
 * Tham số: caller: PCB; reg: region ID.
 * Trả về: 0 nếu thành công, mã lỗi nếu thất bại.
 */
int libfree(struct pcb_t *caller, uint32_t reg)
{
    struct sc_regs request = {0};
    request.a1 = SYSMEM_REGION_FREE;
    request.a2 = reg;
    request.flags = SC_INTERNAL;
    return _syscall(caller->krnl, caller->pid, 17, &request);
}

/* libread — Wrapper READ: gửi region và offset, nhận byte qua a6.
 * Tham số: caller: PCB; reg: region; offset: độ lệch; out: nơi nhận byte dạng uint32_t.
 * Trả về: 0 nếu thành công, -1 nếu lỗi.
 */
int libread(struct pcb_t *caller, uint32_t reg, addr_t offset, uint32_t *out)
{
    struct sc_regs request = {0};
    request.a1 = SYSMEM_REGION_READ;
    request.a2 = reg;
    request.a3 = offset;
    request.flags = SC_INTERNAL;
    if (!out || _syscall(caller->krnl, caller->pid, 17, &request)) {
        return -1;
    }
    *out = (uint32_t)request.a6;
    printf("[READ] PID=%u reg=%u offset=%" PRIu64 " value=%u\n", caller->pid, reg, offset, *out);
    return 0;
}

/* libwrite — Wrapper WRITE: gửi giá trị byte, region, offset qua syscall.
 * Tham số: caller: PCB; data: byte; reg: region; offset: độ lệch.
 * Trả về: 0 nếu thành công, mã lỗi nếu thất bại.
 */
int libwrite(struct pcb_t *caller, BYTE data, uint32_t reg, addr_t offset)
{
    struct sc_regs request = {0};
    request.a1 = SYSMEM_REGION_WRITE;
    request.a2 = reg;
    request.a3 = offset;
    request.a4 = (unsigned char)data;
    request.flags = SC_INTERNAL;
    return _syscall(caller->krnl, caller->pid, 17, &request);
}
/* Kernel có bảng trang chung, descriptor region nằm trong PCB sở hữu. */
/* __kmalloc — Cấp phát region kernel từ dãy frame liên tục, lưu descriptor trong PCB.
 * Tham số: caller: PCB; id: VMA 0; reg: register; size: số byte; out: VA kernel đầu ra.
 * Trả về: 0 nếu thành công, (addr_t)-1 nếu lỗi.
 */
addr_t __kmalloc(struct pcb_t *caller, int id, int reg, addr_t size, addr_t *out)
{
    int result = -1;
    pthread_mutex_lock(&memory_lock);
    struct vm_rg_struct *symbol_region = get_symrg_byid(caller->mm, reg);
    struct mm_struct *mm = caller->krnl->mm;
    struct vm_area_struct *vma = mm->mmap;
    // Bước 1: Kiểm tra symbol trống và size hợp lệ.
    if (symbol_region == NULL || id != 0 || out == NULL || size == 0 ||
        size > 8192ULL * PAGING64_PAGESZ) {
        goto done;
    }
    if (symbol_region->rg_end > symbol_region->rg_start) {
        goto done;
    }
    // Bước 2: Làm tròn kích thước lên số trang, cấp phát liên tục.
    addr_t page_count = (size + 4095) / PAGING64_PAGESZ;
    addr_t virtual_address = vma->vm_end;
    struct vm_rg_struct mapped;
    if (page_count > (UINT64_MAX - virtual_address) / 4096 || vm_map_kernel(caller, vma->vm_start, virtual_address + page_count*4096, virtual_address, (int)page_count, &mapped)) {
        goto done;
    }
    // Bước 3: Ghi descriptor kernel vào symbol của process sở hữu.
    *symbol_region = (struct vm_rg_struct){0};
    symbol_region->vmaid = id;
    symbol_region->mode_bit = 0;
    symbol_region->rg_start = virtual_address;
    symbol_region->rg_end = virtual_address + size;
    vma->vm_end = vma->sbrk = virtual_address + page_count*4096;
    *out = virtual_address;
    caller->regs[reg] = virtual_address;
    result = 0;
#ifdef IODUMP
    printf("[REGION] PID=%u reg=%d mode=kernel range=[%016" PRIx64 ",%016" PRIx64 ")\n", caller->pid, reg, symbol_region->rg_start, symbol_region->rg_end);
#endif

done:
    pthread_mutex_unlock(&memory_lock);
    if (result != 0) {
        return (addr_t)-1;
    }
    return 0;
}
/* libkmem_malloc — Wrapper KMALLOC, giữ kiểu uint32_t của framework.
 * Tham số: caller: PCB; size: số byte; reg: register đích.
 * Trả về: 0 nếu thành công, -1 nếu lỗi.
 */
int libkmem_malloc(struct pcb_t *caller, uint32_t size, uint32_t reg)
{
    struct sc_regs request = {0};
    request.a1 = SYSMEM_KMALLOC;
    request.a2 = reg;
    request.a3 = size;
    request.flags = SC_INTERNAL;
    if (_syscall(caller->krnl, caller->pid, 17, &request)) {
        return -1;
    }
    printf("[KMALLOC] PID=%u reg=%u VA=%016" PRIx64 " size=%u\n", caller->pid, reg, request.a6, size);
    return 0;
}

/* libkmem_copy_from_user — Wrapper sao chép từ user region sang kernel region qua syscall.
 * Tham số: caller: PCB; src/dst: region nguồn/đích; offset: độ lệch chung; size: số byte.
 * Trả về: 0 nếu thành công, mã lỗi nếu thất bại.
 */
int libkmem_copy_from_user(struct pcb_t *caller, uint32_t src, uint32_t dst, uint32_t offset, uint32_t size)
{
    struct sc_regs request = {0};
    request.a1 = SYSMEM_COPY_FROM;
    request.a2 = src;
    request.a3 = dst;
    request.a4 = offset;
    request.a5 = size;
    request.flags = SC_INTERNAL;
    return _syscall(caller->krnl, caller->pid, 17, &request);
}

/* libkmem_copy_to_user — Wrapper sao chép từ kernel region sang user region qua syscall.
 * Tham số: caller: PCB; src/dst: region nguồn/đích; offset: độ lệch chung; size: số byte.
 * Trả về: 0 nếu thành công, mã lỗi nếu thất bại.
 */
int libkmem_copy_to_user(struct pcb_t *caller, uint32_t src, uint32_t dst, uint32_t offset, uint32_t size)
{
    struct sc_regs request = {0};
    request.a1 = SYSMEM_COPY_TO;
    request.a2 = src;
    request.a3 = dst;
    request.a4 = offset;
    request.a5 = size;
    request.flags = SC_INTERNAL;
    return _syscall(caller->krnl, caller->pid, 17, &request);
}

/* libmem_dma_memcpy — Giữ giao diện DMA của framework; chưa tích hợp co-processor trong phần
 * này.
 * Tham số: caller: PCB; src/dst: region; offset: độ lệch; size: số byte (chưa dùng).
 * Trả về: -ENOSYS, không báo thành công giả.
 */
int libmem_dma_memcpy(struct pcb_t *caller, uint32_t src, uint32_t dst, uint32_t offset, uint32_t size)
{
    /* DMA thuộc phần co-processor. Không báo thành công khi chưa tích hợp bus. */
    (void)caller;
    (void)src;
    (void)dst;
    (void)offset;
    (void)size;
    return -ENOSYS;
}
/* free_pcb_memph — Thu hồi region kernel, frame user, cây bảng trang, VMA; gỡ PID khỏi
 * registry.
 * Tham số: caller: PCB cần thu hồi MM (PCB giả đối với kernel lúc shutdown).
 * Trả về: 0 nếu thành công, -1 nếu PCB/MM NULL. Không free bản thân PCB.
 */
int free_pcb_memph(struct pcb_t *caller)
{
    if (!caller || !caller->mm) {
        return -1;
    }
    pthread_mutex_lock(&memory_lock);
    // Bước 1: Gỡ PID khỏi registry trước khi hủy MM.
    struct pcb_t *current = caller->krnl->processes;
    struct pcb_t *previous = NULL;
    while (current != NULL && current != caller) {
        previous = current;
        current = current->registry_next;
    }
    if (current != NULL) {
        if (previous == NULL) {
            caller->krnl->processes = caller->registry_next;
        } else {
            previous->registry_next = caller->registry_next;
        }
    }
    caller->registry_next = NULL;
    for (int i = 0; i < REG_COUNT; ++i) {
        struct vm_rg_struct *region = get_symrg_byid(caller->mm, i);
        if (region && region->rg_end > region->rg_start && !region->mode_bit) {
            __free(caller, 0, i);
        }
    }
    struct mm_struct *mm = caller->mm;
    // Bước 2: In thống kê trước khi thu hồi cây bảng trang.
#ifdef PAGETBL_DUMP
    print_pgtbl(caller, 0, UINT64_MAX);
#else
    print_pgtbl(caller, 1, 0);
#endif
    // DFS postorder: trả frame ở PT trước, sau đó free bảng đã duyệt hết.
    addr_t *tables_by_level[5] = {mm->pgd};
    int next_entry[5] = {0};
    int depth = 0;
    if (tables_by_level[0]) {
        while (depth >= 0) {
            if (next_entry[depth] == 512) {
                free(tables_by_level[depth]);
                --depth;
                continue;
            }
            int i = next_entry[depth]++;
            if (!tables_by_level[depth][i]) {
                continue;
            }
            if (depth < 4) {
                tables_by_level[depth + 1] = (addr_t *)(uintptr_t)tables_by_level[depth][i];
                ++depth;
                next_entry[depth] = 0;
            } else if (PAGING_PAGE_PRESENT(tables_by_level[depth][i])) {
                MEMPHY_put_freefp(caller->krnl->mram, PAGING_FPN(tables_by_level[depth][i]));
            }
        }
    }
    // Bước 3: Thu hồi VMA, free region và danh sách FIFO.
    while (mm->mmap != NULL) {
        struct vm_area_struct *vma = mm->mmap;
        mm->mmap = vma->vm_next;
        while (vma->vm_freerg_list) {
            struct vm_rg_struct *free_region = vma->vm_freerg_list;
            vma->vm_freerg_list = free_region->rg_next;
            free(free_region);
        }
        free(vma);
    }
    while (mm->fifo_pgn != NULL) {
        struct pgn_t *page_node = mm->fifo_pgn;
        mm->fifo_pgn = page_node->pg_next;
        free(page_node);
    }
    free(mm);
    caller->mm = NULL;
    pthread_mutex_unlock(&memory_lock);
    return 0;
}

/* find_victim_page — Lấy page cũ nhất khỏi danh sách FIFO nếu có (chưa được gọi để eviction tự
 * động).
 * Tham số: mm: memory manager; out: nơi nhận số trang.
 * Trả về: 0 nếu có page, -1 nếu danh sách rỗng/lỗi.
 */
int find_victim_page(struct mm_struct *mm, addr_t *out)
{
    if (mm == NULL || out == NULL || mm->fifo_pgn == NULL) {
        return -1;
    }
    // Danh sách thêm ở đầu, page cũ nhất ở cuối.
    struct pgn_t *previous = NULL;
    struct pgn_t *current = mm->fifo_pgn;
    while (current->pg_next != NULL) {
        previous = current;
        current = current->pg_next;
    }
    *out = current->pgn;
    if (previous == NULL) {
        mm->fifo_pgn = NULL;
    } else {
        previous->pg_next = NULL;
    }
    free(current);
    return 0;
}
