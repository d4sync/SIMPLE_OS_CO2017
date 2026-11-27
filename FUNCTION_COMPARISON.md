# Đối chiếu hàm và prototype — v2

Nguồn chuẩn: `hk261_ossim_demacOprx.zip` (không phải ZIP v1). Phân tích AST bằng Clang, cấu hình MM64/MLQ_SCHED mặc định. So sánh kiểu hàm (`qualType`: kiểu trả về và danh sách kiểu tham số) của tất cả định nghĩa trong 14 file C được sửa, bao gồm nhánh `co_processor` không bị tắt. Các file C/C++ không sửa giữ nguyên từng byte. Tên biến tham số có thể khác, không làm đổi prototype. Các prototype header hiện hữu giữ nguyên; chỉ thêm khai báo của hàm đã có định nghĩa `free_pcb_memph()` và `enlist_vm_freerg_list()` tại nơi cần dùng.

| Chỉ tiêu | Kết quả |
|---|---:|
| Hàm gốc bị xóa | 0 |
| Hàm gốc đổi kiểu trả về/tham số | 0 |
| Tên hàm/API chưa có trong framework được thêm | 0 |
| Prototype gốc chưa có thân hàm, nay được hiện thực | 1 (`vm_map_kernel`) |
| Định nghĩa gốc trong các file C chỉnh sửa | 98 |
| Định nghĩa v2 trong các file C chỉnh sửa | 99 |
| File input/output gốc giữ nguyên byte | 43/43 |

## Số lượng hàm theo file

| File | Gốc | v2 | Giải thích |
|---|---:|---:|---|
| `src/cpu.c` | 6 | 6 | Giữ nguyên tên và kiểu hàm |
| `src/libmem.c` | 25 | 25 | Giữ nguyên tên và kiểu hàm |
| `src/libstd.c` | 1 | 1 | Giữ nguyên tên và kiểu hàm |
| `src/loader.c` | 2 | 2 | Giữ nguyên tên và kiểu hàm |
| `src/mm-memphy.c` | 10 | 10 | Giữ nguyên tên và kiểu hàm |
| `src/mm-vm.c` | 5 | 5 | Giữ nguyên tên và kiểu hàm |
| `src/mm64.c` | 21 | 22 | Hiện thực `vm_map_kernel` đã khai báo trong `mm.h` |
| `src/os.c` | 5 | 5 | Giữ nguyên tên và kiểu hàm |
| `src/queue.c` | 4 | 4 | Giữ nguyên tên và kiểu hàm |
| `src/sched.c` | 8 | 8 | Giữ nguyên tên và kiểu hàm |
| `src/sys_listsyscall.c` | 1 | 1 | Giữ nguyên tên và kiểu hàm |
| `src/sys_mem.c` | 1 | 1 | Giữ nguyên tên và kiểu hàm |
| `src/syscall.c` | 2 | 2 | Giữ nguyên tên và kiểu hàm |
| `src/timer.c` | 7 | 7 | Giữ nguyên tên và kiểu hàm |

## Các prototype có giới hạn cần giữ

```c
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg);
int libkmem_malloc(struct pcb_t *, uint32_t, uint32_t);
int libkmem_copy_from_user(struct pcb_t*, uint32_t, uint32_t, uint32_t, uint32_t);
int libkmem_copy_to_user(struct pcb_t*, uint32_t, uint32_t, uint32_t, uint32_t);
int libmem_dma_memcpy(struct pcb_t*, uint32_t, uint32_t, uint32_t, uint32_t);
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller);
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller);
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller);
int _syscall(struct krnl_t*, uint32_t, uint32_t, struct sc_regs*);
```

`run()` kiểm tra trước khi truyền đối số 64 bit vào API 32 bit; `__alloc` từ chối size > INT_MAX trước khi gọi `get_free_vmrg_area`. Các hàm `pg_getval`/`pg_setval` giữ giới hạn `int` của skeleton, đường truy cập region 64 bit sử dụng `__read`, `__write`, `__read_kernel_mem`, `__write_kernel_mem`. Không đổi các kiểu này sang `addr_t` như bản v1.

## Các hàm phụ v1 được loại bỏ

| Nhóm | Thay thế bằng |
|---|---|
| `walk`, `new_table`, `empty_table`, `prune`, `mm_canonical`, `va_from_pgn` | Duyệt/khởi tạo/thu hồi nhánh trực tiếp trong `pte_get_entry`, `pte_set_entry`, `get_pd_from_address`, `get_pd_from_pagenum`, `init_mm` |
| `mm_map_pages`, `mm_unmap_pages`, `mm_translate` | `alloc_pages_range`, `vmap_page_range`, `vm_map_ram`, `vm_map_kernel`, `__free`, các hàm đọc/ghi gốc |
| `destroy_table`, `mm_destroy`, `dump_table`, `mm_stats` | DFS lặp 5 cấp trong `free_pcb_memph` và `print_pgtbl` |
| `MEMPHY_get_contiguous`, `MEMPHY_set_owner`, `MEMPHY_owned`, `MEMPHY_destroy` | `vm_map_kernel`, `vmap_page_range`, kiểm tra owner tại handler, cleanup trong `main` |
| `vm_insert_free` | `enlist_vm_freerg_list` |
| `valid_id`, `live`, `region_ok`, `access_byte`, `trace_regions`, `memcall`, `region_call`, `mm_copy_region` | Kiểm tra và syscall trong các hàm libmem gốc; copy trong `__sys_memmap`; log trong ALLOC/FREE/KMALLOC |
| `mm_register`, `mm_unregister`, `mm_lookup` | `ld_routine`, `free_pcb_memph`, `_syscall`, `__sys_memmap` |
| `init_lock`, `mm_lock`, `mm_unlock` | Biến `memory_lock` recursive, gọi trực tiếp pthread |
| `read_line`, `remove_running_proc` | Đọc dòng trong `read_config`; `put_proc` xóa process đã hoàn thành khỏi running list |

## Phạm vi kiểm tra

- File `tests/test_mm.c` là chương trình kiểm thử độc lập, có hàm hỗ trợ kiểm thử, không liên kết vào `os-mm`/`os`. Ràng buộc không thêm hàm áp dụng cho mã nguồn chính.
- `vm_map_kernel` có prototype nguyên bản trong `include/mm.h`; không phải API do v2 tạo ra. Các khai báo gốc chưa có thân hàm ngoài phạm vi MM (ví dụ cache pool) không được hiện thực tùy tiện.
- Audit chạy theo cấu hình môn học MM64/MLQ mặc định, không chứng nhận các chế độ legacy bị vô hiệu hóa. Không chỉnh sửa `mm.c`, `mem.c`, `paging.c` legacy và RTL/C++.
- Log đối chiếu Clang: `tests/results/function_audit.log`.
