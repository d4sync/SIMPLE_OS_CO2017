/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */
/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */
//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */
#include "mm64.h"
#include <stdlib.h>

/* get_vma_by_num — Tìm VMA theo ID trong memory manager.
 * Tham số: mm: memory manager; id: ID VMA.
 * Trả về: Con trỏ VMA hoặc NULL nếu không tìm thấy.
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int id)
{
    if (!mm || id < 0) {
        return NULL;
    }
    for (struct vm_area_struct *vma = mm->mmap; vma; vma = vma->vm_next) {
        if (vma->vm_id == (unsigned long)id) {
            return vma;
        }
    }
    return NULL;
}
/* Cac khoang la [start,end), hai khoang ke nhau khong overlap. */
/* validate_overlap_vm_area — Kiểm tra khoảng mới nằm trong user space và không chồng VMA khác.
 * Tham số: caller: PCB; id: VMA cần mở rộng; start/end: khoảng nửa mở.
 * Trả về: 0 nếu hợp lệ, -1 nếu lỗi.
 */
int validate_overlap_vm_area(struct pcb_t *caller, int id, addr_t start, addr_t end)
{
    struct vm_area_struct *current_vma = get_vma_by_num(caller->mm, id);
    if (!current_vma || start >= end || end > UINT64_C(0x0100000000000000) || end > caller->krnl->kernel_base) {
        return -1;
    }
    for (struct vm_area_struct *vma = caller->mm->mmap; vma; vma = vma->vm_next) {
        if (vma != current_vma && OVERLAP(start, end, vma->vm_start, vma->vm_end)) {
            return -1;
        }
    }
    return 0;
}
/* Chen theo thu tu dia chi va gop cac khoang lien ke. Node da cap phat
 * truoc khi map RAM, nen khong mat region neu malloc that bai. */
/* Ham goc: tao descriptor tai break, chua thay doi VMA. */
/* get_vm_area_node_at_brk — Tạo descriptor vùng mới tại cuối VMA, chưa thay đổi break hay RAM.
 * Tham số: caller: PCB; vmaid: ID VMA; alignedsz: kích thước đã căn page.
 * Trả về: Con trỏ region hoặc NULL nếu lỗi.
 */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, addr_t alignedsz)
{
    struct vm_area_struct *vma = get_vma_by_num(caller->mm, vmaid);
    if (!vma || !alignedsz || alignedsz > UINT64_MAX - vma->vm_end) {
        return NULL;
    }
    struct vm_rg_struct *region = init_vm_rg(vma->vm_end, vma->vm_end + alignedsz);
    if (region) {
        region->vmaid = vmaid;
    }
    return region;
}
extern int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt);
/* inc_vma_limit — Mở rộng VMA theo page, map RAM rồi mới đưa region vào free list.
 * Tham số: caller: PCB; id: VMA; size: số byte cần tăng.
 * Trả về: 0 nếu thành công, -1 nếu lỗi. Caller giữ memory_lock.
 */
int inc_vma_limit(struct pcb_t *caller, int id, addr_t size)
{
    struct vm_area_struct *vma = get_vma_by_num(caller->mm, id);
    if (!vma || !size || size > 8192ULL*4096) {
        return -1;
    }
    // Bước 1: Làm tròn lên bội số 4096, xóa 12 bit thấp.
    addr_t aligned = (size + 4095) & ~UINT64_C(4095);
    addr_t start = vma->vm_end;
    if (aligned > UINT64_MAX - start || validate_overlap_vm_area(caller, id, vma->vm_start, start + aligned)) {
        return -1;
    }
    // Bước 2: Tạo descriptor trước khi lấy frame.
    struct vm_rg_struct *region = get_vm_area_node_at_brk(caller, id, aligned);
    if (!region) {
        return -1;
    }
    struct vm_rg_struct mapped;
    if (vm_map_ram(caller, vma->vm_start, start + aligned, start, (int)(aligned / 4096), &mapped)) {
        free(region);
        return -1;
    }
    // Bước 3: Chỉ cập nhật VMA sau khi map RAM thành công.
    region->vmaid = id;
    enlist_vm_freerg_list(caller->mm, region);
    vma->vm_end = vma->sbrk = start + aligned;
    return 0;
}
/* __mm_swap_page — Kiểm tra quyền sở hữu hai frame rồi sao chép RAM sang SWAP đang active.
 * Tham số: caller: PCB; ram/swap: số frame nguồn/đích.
 * Trả về: 0 nếu thành công, -1 nếu lỗi. Không phải page replacement tự động.
 */
int __mm_swap_page(struct pcb_t *caller, addr_t ram, addr_t swap)
{
    if (!caller || !caller->krnl->active_mswp) {
        return -1;
    }
    struct framephy_struct *ram_frame = caller->krnl->mram->used_fp_list, *swap_frame = caller->krnl->active_mswp->used_fp_list;
    while (ram_frame && ram_frame->fpn != ram) {
        ram_frame = ram_frame->fp_next;
    }
    while (swap_frame && swap_frame->fpn != swap) {
        swap_frame = swap_frame->fp_next;
    }
    if (!ram_frame || !swap_frame || ram_frame->owner != caller->mm || swap_frame->owner != caller->mm) {
        return -1;
    }
    return __swap_cp_page(caller->krnl->mram, ram, caller->krnl->active_mswp, swap);
}
