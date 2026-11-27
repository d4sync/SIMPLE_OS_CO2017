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
/* MEMPHY_format — Chia thiết bị nhớ thành các frame hoàn chỉnh, tạo free list tăng dần.
 * Tham số: mp: thiết bị RAM/SWAP; pagesz: kích thước page 4096 byte.
 * Trả về: 0 nếu thành công, -1 nếu lỗi. Chỉ gọi khi danh sách chưa được tạo.
 */
int MEMPHY_format(struct memphy_struct *mp, int pagesz)
{
    if (mp == NULL || pagesz != PAGING64_PAGESZ) {
        return -1;
    }
    if (mp->free_fp_list != NULL || mp->used_fp_list != NULL) {
        return -1;
    }
    // Bước 1: Tạo một node cho mỗi frame hoàn chỉnh, theo thứ tự FPN.
    addr_t frame_count = (addr_t)mp->maxsz / pagesz;
    struct framephy_struct *last_frame = NULL;
    for (addr_t fpn = 0; fpn < frame_count; ++fpn) {
        struct framephy_struct *new_frame = calloc(1, sizeof(*new_frame));
        if (new_frame == NULL) {
            // Nếu hết bộ nhớ host, thu hồi tất cả node vừa tạo.
            while (mp->free_fp_list != NULL) {
                struct framephy_struct *current = mp->free_fp_list;
                mp->free_fp_list = current->fp_next;
                free(current);
            }
            return -1;
        }
        new_frame->fpn = fpn;
        // Bước 2: Nối vào cuối danh sách (không dùng con trỏ cấp hai).
        if (last_frame == NULL) {
            mp->free_fp_list = new_frame;
        } else {
            last_frame->fp_next = new_frame;
        }
        last_frame = new_frame;
    }
    return 0;
}
/* MEMPHY_mv_csr — Cập nhật vị trí cursor cho thiết bị truy cập tuần tự.
 * Tham số: mp: thiết bị; offset: địa chỉ byte vật lý.
 * Trả về: 0 nếu hợp lệ, -1 nếu ngoài phạm vi.
 */
int MEMPHY_mv_csr(struct memphy_struct *mp, addr_t offset)
{
    if (!mp || offset >= (addr_t)mp->maxsz) {
        return -1;
    }
    mp->cursor = (int)offset;
    return 0;
}
/* MEMPHY_seq_read — Đọc một byte bằng cursor trong chế độ tuần tự.
 * Tham số: mp: thiết bị; addr: địa chỉ vật lý; value: nơi nhận byte.
 * Trả về: 0 nếu thành công, -1 nếu lỗi.
 */
int MEMPHY_seq_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
    if (!mp || !value || mp->rdmflg || MEMPHY_mv_csr(mp, addr)) {
        return -1;
    }
    *value = mp->storage[mp->cursor];
    return 0;
}
/* MEMPHY_seq_write — Ghi một byte bằng cursor trong chế độ tuần tự.
 * Tham số: mp: thiết bị; addr: địa chỉ vật lý; value: byte cần ghi.
 * Trả về: 0 nếu thành công, -1 nếu lỗi.
 */
int MEMPHY_seq_write(struct memphy_struct *mp, addr_t addr, BYTE value)
{
    if (!mp || mp->rdmflg || MEMPHY_mv_csr(mp, addr)) {
        return -1;
    }
    mp->storage[mp->cursor] = value;
    return 0;
}
/* init_memphy — Khởi tạo mảng byte và danh sách frame cho RAM/SWAP.
 * Tham số: mp: thiết bị; size: số byte; randomflg: 1 random, 0 sequential.
 * Trả về: 0 nếu thành công (kể cả size=0), -1 nếu lỗi.
 */
int init_memphy(struct memphy_struct *mp, addr_t size, int randomflg)
{
    if (!mp || size > INT_MAX) {
        return -1;
    }
    memset(mp, 0, sizeof(*mp));
    mp->maxsz = (int)size;
    // Chuẩn hóa cờ truy cập về 0 hoặc 1.
    mp->rdmflg = 0;
    if (randomflg != 0) {
        mp->rdmflg = 1;
    }
    if (!size) {
        return 0;
    }
    mp->storage = calloc(1, size);
    if (!mp->storage) {
        return -1;
    }
    if (MEMPHY_format(mp, PAGING64_PAGESZ)) {
        free(mp->storage);
        mp->storage = NULL;
        return -1;
    }
    return 0;
}

/* MEMPHY_read — Đọc một byte từ địa chỉ vật lý theo chế độ của thiết bị.
 * Tham số: mp: thiết bị; addr: địa chỉ vật lý; value: đầu ra.
 * Trả về: 0 nếu thành công, -1 nếu lỗi.
 */
int MEMPHY_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
    if (!mp || !value || addr >= (addr_t)mp->maxsz) {
        return -1;
    }
    if (!mp->rdmflg) {
        return MEMPHY_seq_read(mp, addr, value);
    }
    *value = mp->storage[addr];
    return 0;
}

/* MEMPHY_write — Ghi một byte vào địa chỉ vật lý theo chế độ của thiết bị.
 * Tham số: mp: thiết bị; addr: địa chỉ vật lý; data: byte cần ghi.
 * Trả về: 0 nếu thành công, -1 nếu lỗi.
 */
int MEMPHY_write(struct memphy_struct *mp, addr_t addr, BYTE data)
{
    if (!mp || addr >= (addr_t)mp->maxsz) {
        return -1;
    }
    if (!mp->rdmflg) {
        return MEMPHY_seq_write(mp, addr, data);
    }
    mp->storage[addr] = data;
    return 0;
}

/* MEMPHY_get_freefp — Lấy frame trống đầu tiên, chuyển sang used list và zero-fill.
 * Tham số: mp: thiết bị; fpn: nơi nhận số frame.
 * Trả về: 0 nếu thành công, -1 nếu hết frame/lỗi. Caller giữ memory_lock khi dùng đồng thời.
 */
int MEMPHY_get_freefp(struct memphy_struct *mp, addr_t *fpn)
{
    if (mp == NULL || fpn == NULL) {
        return -1;
    }
    if (mp->free_fp_list == NULL) {
        return -1;
    }
    // Lấy node đầu free list, chuyển lên đầu used list.
    struct framephy_struct *current_frame = mp->free_fp_list;
    mp->free_fp_list = current_frame->fp_next;
    current_frame->fp_next = mp->used_fp_list;
    mp->used_fp_list = current_frame;
    *fpn = current_frame->fpn;
    // Xóa dữ liệu cũ trước khi giao frame cho process khác.
    addr_t physical_address = *fpn * PAGING64_PAGESZ;
    memset(mp->storage + physical_address, 0, PAGING64_PAGESZ);
    return 0;
}

/* MEMPHY_put_freefp — Trả frame từ used list về free list theo FPN tăng dần, xóa owner.
 * Tham số: mp: thiết bị; fpn: số frame cần trả.
 * Trả về: 0 nếu thành công, -1 nếu không tìm thấy/lỗi. Caller giữ memory_lock.
 */
int MEMPHY_put_freefp(struct memphy_struct *mp, addr_t fpn)
{
    if (mp == NULL) {
        return -1;
    }
    // Bước 1: Tìm frame trong danh sách đang sử dụng.
    struct framephy_struct *current = mp->used_fp_list;
    struct framephy_struct *previous = NULL;
    while (current != NULL && current->fpn != fpn) {
        previous = current;
        current = current->fp_next;
    }
    if (current == NULL) {
        return -1; // Không tồn tại hoặc đã FREE.
    }
    // Bước 2: Tháo frame khỏi used list.
    if (previous == NULL) {
        mp->used_fp_list = current->fp_next;
    } else {
        previous->fp_next = current->fp_next;
    }
    current->owner = NULL;
    // Bước 3: Tìm vị trí chèn theo FPN tăng dần (hỗ trợ KMALLOC).
    struct framephy_struct *released_frame = current;
    current = mp->free_fp_list;
    previous = NULL;
    while (current != NULL && current->fpn < fpn) {
        previous = current;
        current = current->fp_next;
    }
    released_frame->fp_next = current;
    if (previous == NULL) {
        mp->free_fp_list = released_frame;
    } else {
        previous->fp_next = released_frame;
    }
    return 0;
}

/* MEMPHY_dump — In các byte khác 0 trong thiết bị để debug.
 * Tham số: mp: thiết bị nhớ.
 * Trả về: 0 nếu thành công, -1 nếu mp NULL.
 */
int MEMPHY_dump(struct memphy_struct *mp)
{
    if (mp == NULL) {
        return -1;
    }
    for (int i = 0; i < mp->maxsz; ++i) {
        if (mp->storage[i]) {
            printf("RAM[%x]=%02x\n", i, (unsigned char)mp->storage[i]);
        }
    }
    return 0;
}
