# Hướng dẫn source v2 — Paging, System Call, USR

## 1. Phạm vi

Dựa trên skeleton demacOprx và PDF Assignment – Simple Operating System, 17/10/2026, mục 1.3, 1.6, 2.2, 2.4, 3.2–3.3. Giữ nguyên mọi prototype gốc, không thêm tên hàm/API mới vào chương trình chính. `vm_map_kernel` đã được khai báo trong `mm.h` gốc, nay được hiện thực. Các thay đổi cấu trúc và macro là cần thiết để quản lý cây, owner, PID registry và thống kê. Không triển khai automatic page replacement khi chưa có yêu cầu giảng viên. DMA chưa thuộc phạm vi phần này. Xem `FUNCTION_COMPARISON.md` và `TEST_RESULTS.md`.

## 2. Compile/run

```bash
make os-mm
./os-mm os_mm_usr_demo
make test-mm
make test-mm MM_TEST_FLAGS='-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie'
```

Chạy Linux/WSL hoặc VM môn học. `make os-mm` không cần Verilator. `make os` cần Verilator và module DMA; chưa kiểm chứng toàn hệ thống vì môi trường thiếu `verilated.h`. Dùng `-iquote include` trong Makefile để tránh header `sched.h` của dự án che khuất header hệ thống khi pthread dùng GNU extension.

## 3. Cấu trúc dữ liệu

- `pcb_t`: PID, 10 regs, PC, code, mm riêng, krnl, registry_next.
- `krnl_t`: danh sách processes, RAM/SWAP, kernel_base và mm kernel chung, `krnl_pgd` trỏ đến root.
- `mm_struct`: PGD, VMA, symbol regions, thống kê bảng/đường dịch. Các con trỏ cấp dưới legacy được giữ lại nhưng cây động dùng entry của bảng cha.
- `vm_rg_struct`: `[rg_start,rg_end)`, mode_bit 1 user / 0 kernel, vmaid, rg_next.
- `vm_area_struct`: VMA0, vm_start, vm_end, sbrk, free list. FREE user trả vùng byte, giữ frame đến khi process kết thúc vì nhiều region có thể chung page.
- `framephy_struct`: FPN, fp_next, owner là mm đang sở hữu. `memphy_struct`: storage, maxsz, random/sequence, free/used lists.

## 4. Physical memory — src/mm-memphy.c

`init_memphy` cấp phát mảng zero-filled; gọi `MEMPHY_format` tạo free list từ FPN 0. `MEMPHY_get_freefp` chuyển node từ free sang used và xóa dữ liệu frame. `MEMPHY_put_freefp` tìm node trong used list, xóa owner, chèn theo FPN tăng dần để tìm vùng liên tục. Double-free trả lỗi. `MEMPHY_read`/`MEMPHY_write` nhận PA, kiểm tra giới hạn; thiết bị tuần tự dùng `MEMPHY_seq_read/write` và `MEMPHY_mv_csr`. Các hàm tầng thấp phải chạy dưới `memory_lock` nếu được gọi đồng thời.

## 5. Paging — src/mm64.c

VA 57 bit canonical: bit 63–57 lặp bit 56. PGD (56–48), P4D (47–39), PUD (38–30), PMD (29–21), PT (20–12), offset (11–0). Mỗi bảng 512 slot x 8 byte = 4096 B. PTE cuối 32 bit nằm trong slot 64 bit; present31, swap30, reserve29, dirty28, USR number27–15 (để 0), FPN12–0. USR number không phải quyền truy cập. PTE present với FPN=0 hợp lệ. 13 bit FPN giới hạn RAM có thể ánh xạ đến 32 MiB; không cắt bit cao khi config RAM lớn hơn.

- `get_pd_from_address`: kiểm tra canonical, tách index bằng dịch bit và mask 511.
- `get_pd_from_pagenum`: khôi phục VA từ PGN, hỗ trợ page number compact 45 bit và page number của VA canonical đầy đủ.
- `init_pte`, `pte_set_fpn`, `pte_set_swap`: mã hóa trạng thái PTE.
- `pte_get_entry`: duyệt từ root qua bốn con trỏ tới PT, không tạo bảng.
- `pte_set_entry`: tạo child bằng calloc khi cần; tăng `tables`, `peak_tables`. Khi xóa entry hoặc lỗi calloc, duyệt ngược path để free bảng rỗng. Không cần `walk` hoặc `prune` riêng.
- `alloc_pages_range`: lấy frame, rollback nếu thiếu. `vmap_page_range` kiểm tra mapping rồi ghi PTE, gán owner. `vm_map_ram` kết hợp hai hàm, giải phóng danh sách tạm; rollback frame nếu mapping lỗi.
- `vm_map_kernel`: prototype sẵn trong mm.h, duyệt free list tìm dãy FPN liền nhau, chuyển toàn bộ sang used rồi map kernel. Nếu map thất bại, xóa PTE đã tạo và trả dãy frame.
- `print_pgtbl`: DFS lặp với 5 con trỏ + 5 chỉ số, in VA/PTE/FPN và số liệu. Không đệ quy bằng hàm mới.

Ví dụ VA `0x39ffe1d9c9000` cho index 003, 13f, 1f8, 0ec, 1c9. VA 4097 ánh xạ FPN 3 thì PA=3*4096+1=12289. Cây sparse chỉ tạo nhánh có dữ liệu. Flat table lý thuyết 128 TiB cho 2^45 PTE 4 byte; một path động 20 KiB. Không có TLB.

## 6. VM — src/mm-vm.c và src/libmem.c

`get_vma_by_num` tìm VMA. `get_vm_area_node_at_brk` tạo descriptor mà chưa cập nhật break. `validate_overlap_vm_area` kiểm tra khoảng hợp lệ, tránh overlap và giới hạn user. `inc_vma_limit` round up 4096, gọi `vm_map_ram`, chỉ cập nhật vm_end/sbrk sau thành công. `enlist_vm_freerg_list` chèn theo địa chỉ và gộp khoảng liền kề. `get_free_vmrg_area` first-fit, cắt vùng đủ size. Prototype `int size` gốc được giữ; `__alloc` kiểm tra INT_MAX trước khi cast.

`liballoc` gửi syscall 17 op6, handler gọi `__alloc`, lấy vùng trống hoặc syscall INC, zero-fill và lưu symbol + register. `__free` trả user region về free list; kernel FREE bỏ PTE và trả frame ngay. `__read` và `__write` kiểm tra region, mode, offset, lấy PTE từ VA 64 bit, xác thực owner rồi gửi syscall IO. WRITE cập nhật dirty. `libread` trả byte qua uint32_t*, CPU lưu vào register đích. Các hàm legacy `pg_getpage`, `pg_getval`, `pg_setval` giữ prototype int và không ép địa chỉ cao vào int.

## 7. System call — src/syscall.c, sys_mem.c, libstd.c

```text
CPU run -> liballoc/libread/... -> _syscall(krnl, pid, nr, regs)
 -> duyệt krnl->processes -> __sys_memmap(krnl, pid, regs)
 -> __alloc/__read/__write/... -> paging -> MEMPHY
```

`_syscall` khóa memory_lock, tìm PID, dispatch bằng syscalltbl.lst (sinh từ syscall.tbl). Handler `__sys_memmap` tìm lại PCB qua PID. Không đưa PCB pointer trực tiếp từ userspace vào dispatcher. Syscall 0 liệt kê 0/17. Syscall 17: 1 MAP (map ở break hoặc zero existing user range), 2 INC, 3 SWP primitive, 4 IO_READ, 5 IO_WRITE. Các op6–12 nội bộ: region alloc/free/read/write, KMALLOC, COPY_FROM, COPY_TO. `SC_INTERNAL` chỉ được set ở wrapper C; `libsyscall` từ instruction không có cờ này nên raw physical IO và op nội bộ bị chặn. Mô phỏng kiểm tra quyền bằng phần mềm, không phải hardware rings.

## 8. USR

User mm riêng; kernel mm chung ở upper canonical base mặc định `0xff00000000000000`, có thể cấu hình base aligned. Kernel region descriptor nằm trong symbol của process chủ. `libkmem_malloc` -> syscall -> `__kmalloc` -> `vm_map_kernel`; kích thước làm tròn theo page nhưng region lưu size yêu cầu. `libkmem_copy_from_user` / `libkmem_copy_to_user` gửi op11/12. Handler xác minh cả source/destination range và mode trước khi copy từng byte qua `__read_user_mem`/`__write_kernel_mem` hoặc chiều ngược lại. Cùng offset cho hai vùng. READ/WRITE trực tiếp kernel region bị từ chối. Các prototype uint32_t size/offset được giữ; CPU từ chối đối số vượt UINT32_MAX trước khi chuyển.

## 9. Đồng bộ và vòng đời

Dùng `pthread_mutex_t memory_lock` recursive (GNU/Linux), trực tiếp gọi pthread_mutex_lock/unlock, không thêm hàm wrapper. Cần recursive vì syscall region có thể gọi syscall INC/IO bên trong. Registry, free list, PTE, VMA, symbol, copy, cleanup cùng được bảo vệ. `ld_routine` đăng ký PID trước khi đưa process vào scheduler. `free_pcb_memph` bỏ PID, thu hồi kernel region, in thống kê, DFS lặp postorder free user frames và table, free VMA và mm. CPU free PCB sau đó. Kernel final dùng descriptor tạm gọi chính `free_pcb_memph`. `main` dọn RAM/SWAP. Không giữ MM lock khi đợi timer hoặc gọi scheduler.

Scheduler/Timer trong ZIP chỉ là bản tích hợp để CPU-only chạy được từ skeleton. `put_proc` phải xóa process đã kết thúc khỏi running list và không enqueue lại. Nếu Scheduler của nhóm khác, không ghi đè tự động; thống nhất xử lý lifecycle tương đương. Timer dùng cùng event_lock để bảo vệ done và atomic time/stop.

## 10. Thống kê

`tables[0..4]` tương ứng PT..PGD; `peak_tables` là tổng số bảng lớn nhất. `bytes` và `peak_bytes` tính 4096 byte/bảng. `walks` và `pt_accesses` đếm lần dịch thành công phục vụ byte truy cập/zero-fill (5 entry mỗi lần), không tính debug/metadata/dirty write-back hay lượt kiểm tra lỗi. `data_accesses` tính số byte đọc/ghi và zero-fill user/MAP; không tính physical-frame initialization hay swap primitive. `[REGION]`, `[FREE]`, `print_list_rg`, `[MM-STATS]` thể hiện allocation và thống kê trong output. Demo kernel: 8 walks, 40 PT, 8 data, total 48.

## 11. Kiểm thử và hạn chế

Xem `TEST_RESULTS.md`, `tests/test_mm.c`, `tests/results/`. 12 nhóm test pass, sanitizer pass; 14 input gốc exit 0, không đảm bảo tất cả instruction hợp lệ. Original input/output nguyên vẹn. Page replacement tự động chưa làm; SWP chỉ sao chép frame, không eviction/page-in; hết RAM rollback. DMA `libmem_dma_memcpy` trả ENOSYS chứ không giả lập thành công. Kernel virtual bump không tái sử dụng địa chỉ ảo sau FREE; frame vật lý được trả ngay. User FREE giữ frame tới exit. Các config MM64/MLQ là phạm vi đã kiểm thử. Full RTL và Scheduler riêng của nhóm chưa kiểm chứng.

## 12. Tích hợp GitHub

Không push hay merge tự động. Tạo backup branch, xem diff và kiểm tra patch trên branch 2412009. Patch gốc→v2 dùng khi branch còn skeleton; patch v1→v2 khi đã áp dụng bản v1. Hai patch là các lựa chọn thay thế, không áp dụng cả hai. Chúng có phần tích hợp Scheduler/Timer cần review; nếu đồng đội sửa những file này, giải quyết conflict thủ công, giữ thuật toán nhóm. Cần test lại trước khi cập nhật PR. Tài liệu và log đi kèm ZIP, không nằm trong patch code.
