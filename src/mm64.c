#define _GNU_SOURCE
/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */
/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */
/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */
#include "mm64.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <inttypes.h>
#include <limits.h>
#ifdef MM64
/* Khóa chung bảo vệ thao tác cấp phát, syscall và vòng đời PCB.
 * Recursive vì syscall region có thể gọi tiếp syscall INC/IO trong cùng thread. */
pthread_mutex_t memory_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

/* get_pd_from_address — Kiểm tra địa chỉ canonical và tách 5 chỉ số bảng trang.
 * Tham số: virtual_address: VA 64 bit; pgd, p4d, pud, pmd, pt: nơi nhận chỉ số.
 * Trả về: 0 nếu hợp lệ, -1 nếu lỗi.
 */
int get_pd_from_address(addr_t virtual_address, addr_t *pgd, addr_t *p4d, addr_t *pud, addr_t *pmd, addr_t *pt)
{
    if (pgd == NULL || p4d == NULL || pud == NULL || pmd == NULL || pt == NULL) {
        return -1;
    }
    // Bước 1: Kiểm tra canonical: bit 63..57 phải lặp lại bit 56.
    addr_t sign_bit = (virtual_address >> 56) & 1ULL;
    addr_t upper_bits = virtual_address >> 57;
    if (sign_bit == 0) {
        if (upper_bits != 0) {
            return -1;
        }
    } else {
        if (upper_bits != 127) {
            return -1;
        }
    }
    // Bước 2: Dịch phải tới bit thấp nhất của mỗi index, giữ 9 bit.
    // 0x1FF = 511 = 9 bit 1; mỗi bảng có 512 entry.
    *pgd = (virtual_address >> 48) & 0x1FFULL;
    *p4d = (virtual_address >> 39) & 0x1FFULL;
    *pud = (virtual_address >> 30) & 0x1FFULL;
    *pmd = (virtual_address >> 21) & 0x1FFULL;
    *pt = (virtual_address >> 12) & 0x1FFULL;
    return 0;
}

/* get_pd_from_pagenum — Đổi số trang ảo thành địa chỉ đầu trang rồi tách chỉ số 5 cấp.
 * Tham số: pgn: số trang; pgd, p4d, pud, pmd, pt: con trỏ đầu ra.
 * Trả về: 0 nếu thành công, -1 nếu lỗi.
 */
int get_pd_from_pagenum(addr_t pgn, addr_t *pgd, addr_t *p4d, addr_t *pud, addr_t *pmd, addr_t *pt)
{
    if (pgn > (UINT64_MAX >> 12)) {
        return -1;
    }
    addr_t virtual_address = pgn << 12;
    // PGN dạng 45 bit của kernel phải được sign-extend về 64 bit.
    if (pgn < PAGING64_MAX_PGN && (virtual_address & (UINT64_C(1) << 56))) {
        virtual_address |= UINT64_C(0xfe00000000000000);
    }
    return get_pd_from_address(virtual_address, pgd, p4d, pud, pmd, pt);
}
/* Mỗi bảng có 512 entry 8 byte. Bốn cấp trên chứa con trỏ host;
 * cấp PT chứa PTE 32 bit trong slot 64 bit. */

/* init_pte — Mã hóa PTE 32 bit theo trạng thái present hoặc swapped.
 * Tham số: pte: đầu ra; pre: present; fpn: frame; drt: dirty; swp: swapped; type: loại SWAP;
 *          offset: vị trí SWAP.
 * Trả về: 0 nếu hợp lệ, -1 nếu trường vượt giới hạn.
 */
int init_pte(addr_t *pte, int pre, addr_t fpn, int drt, int swp, int type, addr_t offset)
{
    if (!pte) {
        return -1;
    }
    if (swp) {
        if (type < 0 || type >= PAGING_MAX_MMSWP || offset > (PAGING_PTE_SWPOFF_MASK >> 5)) {
            return -1;
        }
        // Bit 30: swapped; bit 4..0: thiết bị; bit 25..5: swap offset.
        *pte = PAGING_PTE_SWAPPED_MASK | ((addr_t)type) | (offset << 5);
    } else if (pre) {
        if (fpn > PAGING_PTE_FPN_MASK) {
            return -1;
        }
        // Đặt bit present và giữ 13 bit FPN.
        *pte = PAGING_PTE_PRESENT_MASK | fpn;
        if (drt != 0) {
            *pte |= PAGING_PTE_DIRTY_MASK;
        }
    } else {
        *pte = 0;
    }
    return 0;
}
/* pte_get_entry — Duyệt cây PGD đến PT để lấy PTE, không cấp phát bảng mới.
 * Tham số: mm: cây bảng trang; pgn: số trang; caller: PCB (giữ theo prototype, không dùng).
 * Trả về: Giá trị PTE; 0 nếu chưa map hoặc tham số lỗi.
 */
addr_t pte_get_entry(struct mm_struct *mm, addr_t pgn, struct pcb_t *caller)
{
    (void)caller;
    addr_t page_indices[5];
    if (mm == NULL || mm->pgd == NULL) {
        return 0;
    }
    if (get_pd_from_pagenum(pgn, &page_indices[0], &page_indices[1],
                           &page_indices[2], &page_indices[3], &page_indices[4]) != 0) {
        return 0;
    }
    addr_t *table = mm->pgd;
    // Bắt đầu PGD; đi qua 4 directory chứa con trỏ host tới bảng con.
    for (int level = 0; level < 4; ++level) {
        if (!table[page_indices[level]]) {
            return 0;
        }
        // Entry ở 4 cấp trên là địa chỉ host, không phải FPN trong RAM mô phỏng.
        table = (addr_t *)(uintptr_t)table[page_indices[level]];
    }
    // PT chứa PTE 32 bit trong slot 64 bit. Đọc PTE, không tạo bảng.
    return table[page_indices[4]];
}

/* pte_set_entry — Ghi PTE, cấp phát bảng con khi cần; khi xóa hoặc thất bại thu hồi bảng rỗng.
 * Tham số: mm: cây bảng trang; pgn: số trang; value: PTE 32 bit; caller: PCB (không dùng).
 * Trả về: 0 nếu thành công, -1 nếu lỗi. Caller giữ memory_lock.
 */
int pte_set_entry(struct mm_struct *mm, addr_t pgn, uint64_t value, struct pcb_t *caller)
{
    (void)caller;
    addr_t page_indices[5];
    addr_t *tables_by_level[5];
    if (mm == NULL || mm->pgd == NULL || value > UINT32_MAX) {
        return -1;
    }
    if (get_pd_from_pagenum(pgn, &page_indices[0], &page_indices[1],
                           &page_indices[2], &page_indices[3], &page_indices[4]) != 0) {
        return -1;
    }
    tables_by_level[0] = mm->pgd;
    int depth = 0;
    int result = 0;
    // Bước 1: Cấp phát động bảng cấp dưới chỉ khi cần ghi PTE khác 0.
    while (depth < 4) {
        addr_t index = page_indices[depth];
        if (tables_by_level[depth][index] == 0) {
            if (value == 0) {
                break;
            }
            addr_t *child = calloc(MM64_ENTRIES, sizeof(addr_t));
            if (!child) {
                result = -1;
                break;
            }
            tables_by_level[depth][index] = (addr_t)(uintptr_t)child;
            // Thống kê lưu theo PT..PGD, ngược thứ tự depth PGD..PT.
            ++mm->tables[3 - depth];
            uint64_t table_count = 0;
            for (int i = 0; i < 5; ++i) {
                table_count += mm->tables[i];
            }
            if (table_count > mm->peak_tables) {
                mm->peak_tables = table_count;
            }
        }
        tables_by_level[depth + 1] = (addr_t *)(uintptr_t)tables_by_level[depth][index];
        ++depth;
    }
    // Bước 2: Đã tới PT, ghi PTE vào entry cuối.
    if (depth == 4) {
        tables_by_level[4][page_indices[4]] = value;
    }
    // Bước 3: Thu hồi nhánh rỗng từ dưới lên sau unmap hoặc khi cấp phát lỗi.
    if (value == 0 || result != 0) {
        for (int level = depth; level > 0; --level) {
            int is_empty = 1;
            for (int j = 0; j < MM64_ENTRIES; ++j) {
                if (tables_by_level[level][j]) {
                    is_empty = 0;
                    break;
                }
            }
            if (is_empty == 0) {
                break;
            }
            free(tables_by_level[level]);
            --mm->tables[4 - level];
            tables_by_level[level - 1][page_indices[level - 1]] = 0;
        }
    }
    return result;
}

/* pte_set_fpn — Tạo PTE present cho frame RAM rồi ghi vào cây bảng trang.
 * Tham số: mm: bảng trang; pgn: số trang ảo; fpn: số frame; caller: PCB.
 * Trả về: 0 nếu thành công, -1 nếu lỗi.
 */
int pte_set_fpn(struct mm_struct *mm, addr_t pgn, addr_t fpn, struct pcb_t *caller)
{
    addr_t pte;
    if (init_pte(&pte, 1, fpn, 0, 0, 0, 0)) {
        return -1;
    }
    return pte_set_entry(mm, pgn, pte, caller);
}
/* pte_set_swap — Ghi PTE swapped cho trang của process (không tự thực hiện eviction).
 * Tham số: caller: PCB; pgn: số trang; type: thiết bị SWAP; offset: vị trí trong SWAP.
 * Trả về: 0 nếu thành công, -1 nếu lỗi.
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int type, addr_t offset)
{
    addr_t pte;
    if (!caller || init_pte(&pte, 0, 0, 0, 1, type, offset)) {
        return -1;
    }
    return pte_set_entry(caller->mm, pgn, pte, caller);
}
/* FPN 13 bit: chỉ ánh xạ được tối đa 8192 frame (32 MiB RAM). */

/* vmap_pgd_memset — MAP: mở rộng VMA tại break hoặc xóa dữ liệu của các page đã map thuộc
 * process.
 * Tham số: caller: PCB; virtual_address: VA đầu trang; count: số page.
 * Trả về: 0 nếu thành công, -1 nếu phạm vi, quyền hoặc cấp phát không hợp lệ.
 */
int vmap_pgd_memset(struct pcb_t *caller, addr_t virtual_address, int count)
{
    if (!caller || count <= 0 || count > 8192 || (virtual_address&4095) || virtual_address >= UINT64_C(0x0100000000000000)) {
        return -1;
    }
    struct vm_area_struct *vma = get_vma_by_num(caller->mm, 0);
    if (!vma) {
        return -1;
    }
    addr_t bytes = (addr_t)count*4096;
    if (virtual_address == vma->vm_end) {
        return inc_vma_limit(caller, 0, bytes);
    }
    if (virtual_address < vma->vm_start || virtual_address > vma->vm_end || bytes > vma->vm_end - virtual_address) {
        return -1;
    }
    for (int i = 0; i < count; ++i) {
        addr_t pte = pte_get_entry(caller->mm, (virtual_address >> 12) + i, caller);
        if (!PAGING_PAGE_PRESENT(pte) || (pte&PAGING_PTE_SWAPPED_MASK)) {
            return -1;
        }
        struct framephy_struct *current_frame = caller->krnl->mram->used_fp_list;
        while (current_frame && current_frame->fpn != PAGING_FPN(pte)) {
            current_frame = current_frame->fp_next;
        }
        if (!current_frame || current_frame->owner != caller->mm) {
            return -1;
        }
    }
    for (int i = 0; i < count; ++i) {
        addr_t pgn = (virtual_address >> 12) + i;
        addr_t pte = pte_get_entry(caller->mm, pgn, caller);
        pte_set_entry(caller->mm, pgn, pte | PAGING_PTE_DIRTY_MASK, caller);
        memset(caller->krnl->mram->storage + PAGING_FPN(pte)*4096, 0, 4096);
        ++caller->mm->walks;
        caller->mm->pt_accesses += 5;
        caller->mm->data_accesses += 4096;
    }
    return 0;
}

/* alloc_pages_range — Lấy nhiều frame RAM, tạo danh sách descriptor tạm; rollback nếu thiếu
 * frame.
 * Tham số: proc: PCB; count: số frame; list: nơi nhận đầu danh sách tạm.
 * Trả về: 0 nếu thành công, (addr_t)-1 nếu lỗi. Caller quản lý danh sách kết quả.
 */
addr_t alloc_pages_range(struct pcb_t *proc, int count, struct framephy_struct **list)
{
    if (proc == NULL || list == NULL || count < 0 || count > 8192) {
        return (addr_t)-1;
    }
    *list = NULL;
    struct framephy_struct *last_frame = NULL;
    // Bước 1: Lấy từng frame. Danh sách này chỉ chứa bản sao descriptor.
    for (int i = 0; i < count; ++i) {
        struct framephy_struct *new_frame = calloc(1, sizeof(*new_frame));
        if (new_frame == NULL) {
            goto fail;
        }
        if (MEMPHY_get_freefp(proc->krnl->mram, &new_frame->fpn) != 0) {
            free(new_frame);
            goto fail;
        }
        // PTE chỉ có 13 bit FPN, không được cắt mất các bit cao.
        if (new_frame->fpn > PAGING_PTE_FPN_MASK) {
            MEMPHY_put_freefp(proc->krnl->mram, new_frame->fpn);
            free(new_frame);
            goto fail;
        }
        if (last_frame == NULL) {
            *list = new_frame;
        } else {
            last_frame->fp_next = new_frame;
        }
        last_frame = new_frame;
    }
    return 0;
fail:
    // Bước 2 (nếu lỗi): trả lại mọi frame đã lấy, giải phóng descriptor.
    while (*list != NULL) {
        struct framephy_struct *current = *list;
        *list = current->fp_next;
        MEMPHY_put_freefp(proc->krnl->mram, current->fpn);
        free(current);
    }
    return (addr_t)-1;
}
/* vmap_page_range — Ánh xạ danh sách frame đã lấy vào các trang user; gán owner và rollback
 * mapping khi lỗi.
 * Tham số: proc: PCB; virtual_address: VA đầu; count: số page; frames: danh sách frame; rg:
 *          vùng kết quả.
 * Trả về: 0 nếu thành công, (addr_t)-1 nếu lỗi. Caller trả frame khi thất bại.
 */
addr_t vmap_page_range(struct pcb_t *proc, addr_t virtual_address, int count, struct framephy_struct *frames, struct vm_rg_struct *rg)
{
    if (!proc || !rg || count <= 0 || count > 8192 || (virtual_address&4095) || virtual_address >= UINT64_C(0x0100000000000000) || (addr_t)count > (UINT64_C(0x0100000000000000) - virtual_address) / 4096) {
        return (addr_t)-1;
    }
    // Bước 1: Kiểm tra toàn bộ dãy frame và VA chưa được map.
    struct framephy_struct *current_frame = frames;
    for (int i = 0; i < count; ++i) {
        if (!current_frame || current_frame->fpn > PAGING_PTE_FPN_MASK || pte_get_entry(proc->mm, (virtual_address >> 12) + i, proc)) {
            return (addr_t)-1;
        }
        current_frame = current_frame->fp_next;
    }
    // Bước 2: Ghi từng PTE và gán owner của frame thật trong used list.
    int mapped_count = 0;
    for (current_frame = frames; mapped_count < count;
         current_frame = current_frame->fp_next, ++mapped_count) {
        if (pte_set_fpn(proc->mm, (virtual_address >> 12) + mapped_count, current_frame->fpn, proc)) {
            break;
        }
        for (struct framephy_struct *used_frame = proc->krnl->mram->used_fp_list;
             used_frame != NULL; used_frame = used_frame->fp_next) {
            if (used_frame->fpn == current_frame->fpn) {
                used_frame->owner = proc->mm;
                break;
            }
        }
    }
    // Bước 3: Nếu lỗi, xóa các mapping đã tạo; vm_map_ram sẽ trả frame.
    if (mapped_count < count) {
        for (int j = 0; j < mapped_count; ++j) {
            pte_set_entry(proc->mm, (virtual_address >> 12) + j, 0, proc);
        }
        return (addr_t)-1;
    }
    *rg = (struct vm_rg_struct){.mode_bit = 1,.rg_start = virtual_address,.rg_end = virtual_address + (addr_t)count*4096};
    return 0;
}
/* vm_map_ram — Cấp phát và map RAM cho khoảng user; giải phóng descriptor tạm, trả frame khi
 * lỗi.
 * Tham số: proc: PCB; start/end: giới hạn VMA; virtual_address: VA đầu; count: số page; rg: đầu
 *          ra.
 * Trả về: 0 nếu thành công, (addr_t)-1 nếu lỗi.
 */
addr_t vm_map_ram(struct pcb_t *proc, addr_t start, addr_t end, addr_t virtual_address, int count, struct vm_rg_struct *rg)
{
    if (!proc || !rg || count <= 0 || count > 8192 || (virtual_address&4095) || virtual_address < start || virtual_address > end || end > UINT64_C(0x0100000000000000) || (addr_t)count > (end - virtual_address) / 4096) {
        return (addr_t)-1;
    }
    struct framephy_struct *frames = NULL;
    if (alloc_pages_range(proc, count, &frames)) {
        return (addr_t)-1;
    }
    addr_t result = vmap_page_range(proc, virtual_address, count, frames, rg);
    while (frames) {
        struct framephy_struct *current_frame = frames;
        frames = current_frame->fp_next;
        if (result) {
            MEMPHY_put_freefp(proc->krnl->mram, current_frame->fpn);
        }
        free(current_frame);
    }
    return result;
}

/* vm_map_kernel — Tìm dãy frame vật lý liên tục và ánh xạ vào bảng trang kernel dùng chung.
 * Tham số: proc: PCB; start/end: giới hạn vùng; virtual_address: VA đầu; count: số page; rg:
 *          đầu ra.
 * Trả về: 0 nếu thành công, (addr_t)-1 nếu lỗi; không để lại mapping dở dang.
 */
addr_t vm_map_kernel(struct pcb_t *proc, addr_t start, addr_t end, addr_t virtual_address, int count, struct vm_rg_struct *rg)
{
    if (proc == NULL || rg == NULL || count <= 0 || count > 8192) {
        return (addr_t)-1;
    }
    if ((virtual_address & 4095) != 0 || virtual_address < start || virtual_address < proc->krnl->kernel_base || virtual_address > end) {
        return (addr_t)-1;
    }
    if ((addr_t)count > (end - virtual_address) / 4096) {
        return (addr_t)-1;
    }
    struct mm_struct *mm = proc->krnl->mm;
    struct memphy_struct *ram = proc->krnl->mram;
    addr_t first_page = virtual_address >> 12;
    // Bước 1: Không ghi đè các trang đã được ánh xạ.
    for (int i = 0; i < count; ++i) {
        if (pte_get_entry(mm, first_page + i, proc) != 0) {
            return (addr_t)-1;
        }
    }
    // Bước 2: Tìm dãy frame liên tiếp trong free list đã sắp xếp.
    struct framephy_struct *run_start = ram->free_fp_list;
    struct framephy_struct *previous = NULL;
    while (run_start != NULL) {
        struct framephy_struct *run_end = run_start;
        int frame_count = 1;
        while (frame_count < count && run_end->fp_next != NULL && run_end->fp_next->fpn == run_end->fpn + 1) {
            run_end = run_end->fp_next;
            ++frame_count;
        }
        if (frame_count == count && run_end->fpn <= PAGING_PTE_FPN_MASK) {
            addr_t first_frame = run_start->fpn;
            // Bước 3: Chuyển cả dãy frame sang used list và xóa dữ liệu cũ.
            if (previous == NULL) {
                ram->free_fp_list = run_end->fp_next;
            } else {
                previous->fp_next = run_end->fp_next;
            }
            run_end->fp_next = ram->used_fp_list;
            ram->used_fp_list = run_start;
            memset(ram->storage + first_frame * 4096, 0, (size_t)count * 4096);
            // Bước 4: Gán owner và ánh xạ từng page vào bảng trang kernel.
            int mapped_count = 0;
            struct framephy_struct *current = run_start;
            for (; mapped_count < count; ++mapped_count) {
                current->owner = mm;
                if (pte_set_fpn(mm, first_page + mapped_count, first_frame + mapped_count, proc) != 0) {
                    break;
                }
                current = current->fp_next;
            }
            if (mapped_count < count) {
                // Rollback: xóa mapping đã tạo và trả toàn bộ dãy frame.
                for (int j = 0; j < mapped_count; ++j) {
                    pte_set_entry(mm, first_page + j, 0, proc);
                }
                for (int j = 0; j < count; ++j) {
                    MEMPHY_put_freefp(ram, first_frame + j);
                }
                return (addr_t)-1;
            }
            *rg = (struct vm_rg_struct){0};
            rg->mode_bit = 0;
            rg->rg_start = virtual_address;
            rg->rg_end = virtual_address + (addr_t)count * 4096;
            return 0;
        }
        previous = run_end;
        run_start = run_end->fp_next;
    }
    return (addr_t)-1;
}

/* __swap_cp_page — Sao chép đúng một page giữa hai thiết bị nhớ; không thực hiện thay thế
 * trang.
 * Tham số: src/dst: thiết bị nguồn/đích; source_frame/destination_frame: số frame.
 * Trả về: 0 nếu thành công, -1 nếu lỗi.
 */
int __swap_cp_page(struct memphy_struct *src, addr_t source_frame, struct memphy_struct *dst, addr_t destination_frame)
{
    if (!src || !dst || source_frame >= (addr_t)src->maxsz / 4096 || destination_frame >= (addr_t)dst->maxsz / 4096) {
        return -1;
    }
    for (addr_t i = 0; i < 4096; ++i) {
        BYTE data;
        if (MEMPHY_read(src, source_frame*4096 + i, &data) || MEMPHY_write(dst, destination_frame*4096 + i, data)) {
            return -1;
        }
    }
    return 0;
}
/* init_mm — Khởi tạo memory manager, PGD và VMA 0, chưa cấp phát bảng con hay frame RAM.
 * Tham số: mm: struct cần khởi tạo; caller: PCB user, NULL đối với kernel.
 * Trả về: 0 nếu thành công, -1 nếu thiếu bộ nhớ host.
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
    if (!mm) {
        return -1;
    }
    memset(mm, 0, sizeof(*mm));
    mm->pgd = calloc(MM64_ENTRIES, sizeof(addr_t));
    if (mm->pgd) {
        mm->tables[4] = mm->peak_tables = 1;
    }
    mm->mmap = calloc(1, sizeof(*mm->mmap));
    if (!mm->pgd || !mm->mmap) {
        free(mm->pgd);
        free(mm->mmap);
        memset(mm, 0, sizeof(*mm));
        return -1;
    }
    mm->mmap->vm_mm = mm;
    // Process có VMA riêng ở nửa dưới; kernel dùng nửa trên.
    if (caller != NULL) {
        mm->mmap->vm_start = 0;
    } else {
        mm->mmap->vm_start = KERNEL_BASE_DEFAULT;
    }
    mm->mmap->vm_end = mm->mmap->sbrk = mm->mmap->vm_start;
    return 0;
}
/* init_vm_rg — Tạo descriptor vùng nhớ user dạng khoảng nửa mở [start,end).
 * Tham số: start/end: địa chỉ đầu/cuối (không gồm end).
 * Trả về: Con trỏ descriptor mới hoặc NULL nếu cấp phát lỗi.
 */
struct vm_rg_struct *init_vm_rg(addr_t start, addr_t end)
{
    struct vm_rg_struct *region = calloc(1, sizeof(*region));
    if (region) {
        region->rg_start = start;
        region->rg_end = end;
        region->mode_bit = 1;
    }
    return region;
}
/* enlist_vm_rg_node — Chèn một node region vào đầu danh sách.
 * Tham số: list: địa chỉ con trỏ đầu danh sách; region: node cần chèn.
 * Trả về: 0 nếu thành công, -1 nếu tham số NULL.
 */
int enlist_vm_rg_node(struct vm_rg_struct **list, struct vm_rg_struct *region)
{
    if (!list || !region) {
        return -1;
    }
    region->rg_next = *list;
    *list = region;
    return 0;
}
/* enlist_pgn_node — Tạo và chèn số trang vào đầu danh sách FIFO.
 * Tham số: list: địa chỉ con trỏ đầu danh sách; pgn: số trang.
 * Trả về: 0 nếu thành công, -1 nếu malloc lỗi.
 */
int enlist_pgn_node(struct pgn_t **list, addr_t pgn)
{
    struct pgn_t *page_node = malloc(sizeof(*page_node));
    if (!page_node) {
        return -1;
    }
    page_node->pgn = pgn;
    page_node->pg_next = *list;
    *list = page_node;
    return 0;
}

/* print_pgtbl — Duyệt cây bảng trang theo DFS và in thống kê số bảng, kích thước, lượt truy
 * cập.
 * Tham số: caller: PCB; start/end: khoảng VA cần in, start > end chỉ in thống kê.
 * Trả về: 0 nếu thành công, -1 nếu thiếu MM/PGD.
 */
int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
    if (!caller || !caller->mm || !caller->mm->pgd) {
        return -1;
    }
    pthread_mutex_lock(&memory_lock);
    struct mm_struct *mm = caller->mm;
    // DFS lặp: lưu bảng, prefix địa chỉ và index tiếp theo của mỗi cấp.
    // Không dùng hàm đệ quy mới vì phải giữ nguyên API gốc.
    addr_t *tables_by_level[5] = {mm->pgd};
    addr_t prefix[5] = {0};
    int next_entry[5] = {0};
    int depth = 0;
    if (start <= end) {
        while (depth >= 0) {
            if (next_entry[depth] == 512) {
                --depth;
                continue;
            }
            int i = next_entry[depth]++;
            if (!tables_by_level[depth][i]) {
                continue;
            }
            addr_t virtual_address = prefix[depth] | ((addr_t)i << (48 - 9*depth));
            if (depth < 4) {
                tables_by_level[depth + 1] = (addr_t *)(uintptr_t)tables_by_level[depth][i];
                prefix[depth + 1] = virtual_address;
                ++depth;
                next_entry[depth] = 0;
            } else {
                if (virtual_address&(UINT64_C(1) << 56)) {
                    virtual_address |= UINT64_C(0xfe00000000000000);
                }
                if (virtual_address >= start && virtual_address <= end) {
                    printf("  VA=%016" PRIx64 " PTE=%08" PRIx64 " FPN=%" PRIu64 "\n", virtual_address, tables_by_level[depth][i], (addr_t)PAGING_FPN(tables_by_level[depth][i]));
                }
            }
        }
    }
    uint64_t table_count = 0;
    for (int i = 0; i < 5; ++i) {
        table_count += mm->tables[i];
    }
    const char *space_name = "user";
    if (mm == caller->krnl->mm) {
        space_name = "kernel";
    }
    uint64_t table_bytes = table_count * MM64_ENTRIES * sizeof(addr_t);
    uint64_t peak_bytes = mm->peak_tables * MM64_ENTRIES * sizeof(addr_t);
    uint64_t total_accesses = mm->pt_accesses + mm->data_accesses;
    printf("[MM-STATS] PID=%u %s PGD=%" PRIu64 " P4D=%" PRIu64
           " PUD=%" PRIu64 " PMD=%" PRIu64 " PT=%" PRIu64
           " bytes=%" PRIu64 " peak_bytes=%" PRIu64 " walks=%" PRIu64
           " pt_access=%" PRIu64 " data_access=%" PRIu64 " total=%" PRIu64 "\n",
           caller->pid, space_name, mm->tables[4], mm->tables[3], mm->tables[2],
           mm->tables[1], mm->tables[0], table_bytes, peak_bytes, mm->walks,
           mm->pt_accesses, mm->data_accesses, total_accesses);
    pthread_mutex_unlock(&memory_lock);
    return 0;
}

/* print_list_fp — In FPN của từng node frame.
 * Tham số: current_frame: đầu danh sách frame.
 * Trả về: 0 sau khi in.
 */
int print_list_fp(struct framephy_struct *current_frame)
{
    for (; current_frame; current_frame = current_frame->fp_next) {
        printf("frame=%" PRIu64 "\n", current_frame->fpn);
    }
    return 0;
}
/* print_list_rg — In khoảng địa chỉ của các region.
 * Tham số: region: đầu danh sách region.
 * Trả về: 0 sau khi in.
 */
int print_list_rg(struct vm_rg_struct *region)
{
    for (; region; region = region->rg_next) {
        printf("region=%" PRIu64 "..%" PRIu64 "\n", region->rg_start, region->rg_end);
    }
    return 0;
}
/* print_list_vma — In khoảng địa chỉ của các VMA.
 * Tham số: vma: đầu danh sách VMA.
 * Trả về: 0 sau khi in.
 */
int print_list_vma(struct vm_area_struct *vma)
{
    for (; vma; vma = vma->vm_next) {
        printf("VMA=%" PRIu64 "..%" PRIu64 "\n", vma->vm_start, vma->vm_end);
    }
    return 0;
}
/* print_list_pgn — In các số trang trong danh sách.
 * Tham số: page_node: đầu danh sách page.
 * Trả về: 0 sau khi in.
 */
int print_list_pgn(struct pgn_t *page_node)
{
    for (; page_node; page_node = page_node->pg_next) {
        printf("page=%" PRIu64 "\n", page_node->pgn);
    }
    return 0;
}
#endif
