# spec.md — Simple Operating System (demacOprx, CO2018 HK261)

Tài liệu này tóm tắt **yêu cầu của đề** và **trạng thái code hiện tại** để cả nhóm (và AI agent) làm việc trên cùng một bản hiểu. Mô tả chi tiết cách nhóm hiện thực từng phần và hướng dẫn chạy test nằm ở [docs/implementation_VI.md](docs/implementation_VI.md).

- Nguồn đặc tả: `hk261_ossim_demacOprx.md` (bản chuyển từ `hk261_ossim_demacOprx.pdf`). Khi hai bản khác nhau (bảng biểu, hình vẽ bị lỗi khi chuyển), **PDF là chuẩn**.
- Starter code gốc của thầy: `Thu_muc_goc_cua_thay/hk261_ossim_demacOprx/` — chỉ để tham chiếu/so sánh, **không sửa**.
- Trạng thái code ghi theo nhánh `tong_hop_code`, commit `baeccc6` (27/11/2026): đã tích hợp scheduler, quản lý bộ nhớ MM64, swap, system call và DMA.

---

## 1. Mục tiêu và thang điểm

Mô phỏng một hệ điều hành đơn giản gồm: bộ lập lịch (scheduler), đồng bộ (synchronization), quản lý bộ nhớ ảo → vật lý (paging), system call và co-processor (DMA).

| Hạng mục | Điểm | Nội dung chấm |
|---|---|---|
| Scheduling | 2 | MLQ scheduler (mục 4) |
| MMU + tách user/kernel space | 2 | Paging, syscall truyền PID, kernel memory (mục 5) |
| Co-processor | 3 | 1đ: thao tác bất đồng bộ (async). 2đ: bus addressing (1đ CPU truy cập qua MMU, 1đ co-processor truy cập bộ nhớ) (mục 7) |
| Báo cáo | 3 | Trả lời câu hỏi, Gantt, trạng thái bộ nhớ, sơ đồ dịch địa chỉ đa cấp (mục 10) |

Làm theo nhóm 4–5 người. Nộp: đặt báo cáo vào thư mục source, nén toàn bộ thành `assignment_STUDENTID.zip`, nộp lên LMS. Code system call theo GNU C coding style (https://www.gnu.org/prep/standards/html_node/Writing-C.html).

---

## 2. Build và chạy

Build trên **Linux/WSL** (Makefile dùng `uname`, `mkdir -p`, Verilator tại `VERILATOR_ROOT`, mặc định `/usr/share/verilator`). Đã kiểm tra với Ubuntu 26.04 trên WSL, gcc 15.2, Verilator 5.032.

```sh
make all                          # ./os đầy đủ (CPU + DMA RTL), cần Verilator
./os <config>                     # <config> là tên file trong input/, ví dụ ./os os_0_mlq_paging
make os-mm                        # ./os-mm không link RTL (DMA_MEMCPY trả -ENOSYS)
make test-mm                      # 12 nhóm test assert cho bộ nhớ/swap/syscall/CPU
make test-dma                     # testbench RTL độc lập của DMA
bash tests/run_teacher_tests.sh   # chạy mọi config trong input/, tóm tắt + giải thích lỗi
sh tests/gantt.sh tests/results/sched.log   # rút Gantt chart từ log
make clean
```

- `src/syscalltbl.sh` sinh `src/syscalltbl.lst` từ `src/syscall.tbl` khi build (file sinh ra, không commit).
- So kết quả với `output/<config>.output`. Loader và các CPU chạy song song nên **có thể có nhiều đáp án đúng**; output chỉ cần giải thích được theo lý thuyết. Output hiện tại có thêm nhiều dòng log (`[ALLOC]`, `[PAGE TABLE]`, `[MM-STATS]`, `[SWAP-*]`, `[DMA-*]`…) nên không khớp từng dòng với mẫu.

### 2.1 File cấu hình hệ thống (`input/<config>`)

Khi `MM_FIXED_MEMSZ` **tắt** (trạng thái hiện tại):

```
[time_slice] [N = số CPU] [M = số process]
[KERNEL_BASE]                                   <- tuỳ chọn
[RAM_SZ] [SWP_SZ_0] [SWP_SZ_1] [SWP_SZ_2] [SWP_SZ_3]
[time_0] [path_0] [priority_0]
...
[time_M-1] [path_M-1] [priority_M-1]
```

- Phải có RAM > 0 và ít nhất 1 SWAP > 0; SWAP = 0 nghĩa là thiết bị đó tắt. SWAP đang hoạt động là thiết bị đầu tiên có kích thước khác 0.
- `KERNEL_BASE` (tuỳ chọn, ví dụ `0xff10000000000000`): địa chỉ đầu kernel space, phải canonical nửa trên, ≥ `0xff00000000000000`, căn 4096. Không có dòng này thì dùng `0xff00000000000000`.
- Nếu thiếu cả dòng bộ nhớ (file input kiểu cũ, ví dụ `os_1_singleCPU_mlq`), `read_config` dùng mặc định RAM 1 MiB + SWAP 16 MiB.
- `path` tương đối với `input/proc/`. `priority` ở đây là **live priority**, ghi đè priority mặc định trong file chương trình (dual priority).

### 2.2 File chương trình (`input/proc/<name>`)

```
[priority_mặc_định] [N = số lệnh]
lệnh_0
...
lệnh_N-1
```

---

## 3. Cấu hình biên dịch (`include/os-cfg.h`)

| Macro | Giá trị hiện tại | Ý nghĩa |
|---|---|---|
| `MLQ_SCHED` | bật | Dùng scheduler MLQ (`sched.h` cũng luôn bật) |
| `MAX_PRIO` | 140 | Số mức ưu tiên (0 = cao nhất) |
| `MM_PAGING` | bật | Quản lý bộ nhớ theo paging (libmem, mm-*) |
| `MM_FIXED_MEMSZ` | **tắt** | Tắt → file input có dòng kích thước bộ nhớ |
| `MM64` | **bật** | Địa chỉ 64-bit, phân trang 5 cấp (`mm64.c`); tắt → 32-bit (`mm.c`, không được bảo trì) |
| `IODUMP` | bật | In `[VMA]` sau alloc/free/kmalloc và `[SWAP-OUT]`/`[SWAP-IN]` |
| `PAGETBL_DUMP` | bật | In `[PAGE TABLE]` + `[MM-STATS]` của process khi nó kết thúc |
| `VMDBG`, `MMDBG` | tắt | Debug |
| `OSSIM_NO_DMA` | chỉ khi build `os-mm`/`test-mm` | Bỏ DMA RTL |

---

## 4. Process, tập lệnh và scheduler

### 4.1 PCB và kernel

- `struct pcb_t` (`include/common.h`): `pid`, `priority` (mặc định, cố định), `prio` (live priority, dùng cho MLQ), `code`, `regs[10]`, `pc`, `mm` (không gian địa chỉ riêng của process), `krnl`, `registry_next` (sổ đăng ký PID của kernel), `page_table` (**obsolete, không dùng**), `bp`.
- `struct krnl_t`: `processes` (sổ đăng ký PCB theo PID), `kernel_base`, `ready_queue`, `running_list`, `mlq_ready_queue`, `mm` (mm của kernel, dùng chung cho mọi process), `mram`, `mswp[]`, `active_mswp`, `active_mswp_id`, `krnl_pgd` (= `mm->pgd` của kernel).

### 4.2 Tập lệnh

Trong chế độ `MM_PAGING`, tham số "reg" của alloc/free/read/write/kmalloc/copy/dma là **chỉ số region 0..9** (`REG_COUNT = 10`, tương ứng 10 thanh ghi của CPU theo trang 5 của đề); địa chỉ đầu region cũng được ghi vào `proc->regs[reg]`.

| Lệnh | Cú pháp | Hàm xử lý (`cpu.c` → `libmem.c`) | Hành vi |
|---|---|---|---|
| CALC | `calc` | `calc()` | Chỉ tốn CPU; vẫn chạy khi PID đang chờ DMA |
| ALLOC | `alloc [size] [rg]` | `liballoc` → `__alloc` | Cấp phát `size` byte vùng user (heap, mode 1) |
| KMALLOC | `kmalloc [size] [rg]` | `libkmem_malloc` → `__kmalloc` | Cấp phát vùng kernel (mode 0), **frame liên tục về vật lý** |
| FREE | `free [rg]` | `libfree` → `__free` | User: trả region về free list; kernel: unmap và trả frame |
| READ | `read [rg] [offset] [dst]` | `libread` → `__read` | Đọc 1 byte tại region `rg` + `offset` vào thanh ghi `dst`. **Chỉ vùng user**. `dst ≥ 10`: vẫn đọc, chỉ in cảnh báo, không lưu |
| WRITE | `write [data] [rg] [offset]` | `libwrite` → `__write` | Ghi 1 byte. **Chỉ vùng user** |
| COPY_FROM_USER | `copy_from_user [src] [dst] [offset] [size]` | `libkmem_copy_from_user` → `mm_copy_region` | src user → dst kernel, cùng offset |
| COPY_TO_USER | `copy_to_user [src] [dst] [offset] [size]` | `libkmem_copy_to_user` → `mm_copy_region` | src kernel → dst user, cùng offset |
| DMA_MEMCPY | `dma_memcpy [src] [dst] [offset] [size]` | `libmem_dma_memcpy` → DMA | Copy **bất đồng bộ** bằng co-processor, cùng offset |
| SYSCALL | `syscall [nr] [a1] [a2] [a3]` | `libsyscall` → `_syscall` | Gọi system call số `nr` |

Lỗi của một lệnh được in thành `[INSTRUCTION-ERROR] PID= PC= opcode= rc=`; process vẫn chạy tiếp lệnh sau.

### 4.3 Scheduler MLQ (Mục 2.1 của đề) — **đã làm**

- `MAX_PRIO` hàng đợi `mlq_ready_queue[prio]`; process vào đúng hàng đợi theo `prio`.
- Mỗi hàng đợi có `slot[prio] = MAX_PRIO - prio` lượt dùng CPU. `get_mlq_proc` duyệt từ prio 0, lấy process ở hàng đợi đầu tiên còn process **và** còn slot (trừ 1 slot mỗi lần dispatch); khi mọi hàng đợi còn process đều hết slot thì nạp lại toàn bộ slot. Trong một hàng đợi: FIFO.
- CPU chạy round-robin theo `time_slice`; hết slice thì `put_proc` đưa về hàng đợi cũ.
- `running_list` chứa các process đang chạy (`get_proc` thêm, `put_proc`/`remove_running_proc` gỡ).
- Không có feedback (không đổi hàng đợi). `run_queue` là legacy, không dùng.

---

## 5. Quản lý bộ nhớ (Mục 2.2 của đề) — **đã làm**

### 5.1 Cấu trúc dữ liệu (`include/os-mm.h`)

- `vm_area_struct`: `[vm_start, vm_end)`, `sbrk`, `vm_freerg_list` (sắp theo địa chỉ, gộp vùng liền kề). Hiện chỉ dùng `vmaid = 0`: user bắt đầu từ VA 0, kernel bắt đầu từ `kernel_base`.
- `vm_rg_struct`: `vmaid`, `mode_bit` (1 user / 0 kernel), `[rg_start, rg_end)`.
- `mm_struct`: `pgd` (gốc cây 5 cấp; `p4d/pud/pmd/pt` chỉ giữ cho tương thích), `mmap`, `symrgtbl[30]` (dùng 0..9), bộ đếm `tables[5]`, `peak_tables`, `walks`, `pt_accesses`, `data_accesses`, `swap_ins`, `swap_outs`.
- `framephy_struct`: `fpn`, `owner`, `pgn`, `stamp` (thời điểm vào RAM), `pin` (số DMA đang dùng).
- `memphy_struct`: RAM hoặc SWAP; `free_fp_list` (sắp tăng dần), `used_fp_list`. 1 RAM + tối đa 4 SWAP.

### 5.2 Dịch địa chỉ 64-bit, phân trang 5 cấp

Page 4 KB, mỗi bảng 512 entry × 8 byte (4 KB).

| Bit | 63–57 | 56–48 | 47–39 | 38–30 | 29–21 | 20–12 | 11–0 |
|---|---|---|---|---|---|---|---|
| Cấp | — | L5 | L4 | L3 | L2 | L1 | offset |
| Bảng | — | PGD | P4D | PUD | PMD | PT | OFFSET |

- Bit 63–57 phải bằng bit 56 (canonical). User: `0x0000000000000000`–`0x00ffffffffffffff`; kernel: từ `kernel_base` trở lên.
- Bảng con được **cấp phát theo nhu cầu**; bảng rỗng sau unmap được giải phóng (prune). Một lần dịch địa chỉ = 5 lần truy cập bảng trang; một nhánh đầy đủ tốn 5 × 4 KB = 20 KB, trong khi bảng phẳng cho 2^45 page sẽ tốn 128 TiB.
- Mỗi process có cây riêng (`proc->mm->pgd`); kernel có một cây dùng chung (`krnl->mm->pgd`) → kernel space giống nhau ở mọi process.

### 5.3 Page Table Entry (32 bit, lưu trong slot 64 bit)

| Bit | Page có mặt (present) | Page bị swap |
|---|---|---|
| 0–12 | FPN | 0–4: swap type (id thiết bị SWAP), 5–25: swap offset (slot) |
| 28 | dirty | — |
| 30 | 0 | swapped = 1 |
| 31 | present = 1 | 0 |

FPN chỉ có 13 bit → tối đa 8192 frame (32 MiB RAM dùng được); config 256 MiB vẫn chạy nhưng chỉ dùng 8192 frame đầu.

### 5.4 Luồng thao tác (Figure 7 của đề)

```
ALLOC:  first-fit trong vm_freerg_list
        └─ không đủ → SYSCALL 17 SYSMEM_INC_OP → inc_vma_limit → vm_map_ram
                       → mm_map_pages (lấy frame; hết RAM thì swap out) → tăng sbrk/vm_end
FREE:   user: region về free list (gộp); kernel: unmap + trả frame
READ/WRITE: mm_translate_fault (page bị swap → swap in) → SYSCALL 17 IO_READ/IO_WRITE
```

### 5.5 Tách user space / kernel space

- Mọi thao tác bộ nhớ của lệnh user đi qua `_syscall(krnl, pid, 17, regs)`: **chỉ truyền PID và giá trị**; handler tự tìm PCB trong sổ đăng ký `krnl->processes` (`mm_lookup`).
- Các memop nội bộ (6–13) và IO_READ/IO_WRITE yêu cầu cờ `SC_INTERNAL` (chỉ wrapper trong libmem đặt cờ này); `syscall 17 4 …` gõ trực tiếp từ chương trình user bị từ chối (`-EPERM`).
- READ/WRITE yêu cầu region `mode_bit = 1`; vùng kernel chỉ truy cập được qua `copy_from_user` / `copy_to_user`.

### 5.6 Thay thế trang (swap) — **đã làm, chính sách FIFO**

Đề để giảng viên quyết định chính sách. Nhóm chọn FIFO toàn hệ thống: victim là page user vào RAM sớm nhất; page kernel và frame đang DMA không bị chọn. Chi tiết ở `docs/implementation_VI.md` mục 4.6.

### 5.7 Thống kê bắt buộc trong báo cáo

`[MM-STATS] PID=.. <space> PGD= P4D= PUD= PMD= PT= bytes= peak_bytes= walks= pt_access= data_access= total= swap_in= swap_out=` được in khi `kmalloc` (`kernel-shared`), khi process kết thúc (`user`, `user-final`) và khi tắt máy (`kernel-final`). `PGD..PT` là số bảng mỗi cấp, `bytes` là dung lượng bảng trang hiện tại, `pt_access` là số lần truy cập entry bảng trang khi dịch địa chỉ.

---

## 6. System call (Mục 1.6 của đề)

| Số | Tên | Handler | File |
|---|---|---|---|
| 0 | listsyscall | `__sys_listsyscall` | `src/sys_listsyscall.c` |
| 17 | memmap | `__sys_memmap` | `src/sys_mem.c` |
| 440 | xxx | `__sys_xxxhandler` (ví dụ mục 1.6.3, in "The first system call parameter …") | `src/sys_xxxhandler.c` |
| khác | — | `-ENOSYS` | `src/syscall.c` |

PID không tồn tại → `-ESRCH`. Memop của syscall 17 (`include/libmem.h`):

| memop | Ý nghĩa | Ai được gọi |
|---|---|---|
| 1 `SYSMEM_MAP_OP` | `vmap_pgd_memset`: mở rộng hoặc zero-fill page user | mọi lời gọi |
| 2 `SYSMEM_INC_OP` | `inc_vma_limit`: tăng giới hạn vùng nhớ | mọi lời gọi |
| 3 `SYSMEM_SWP_OP` | `__mm_swap_page`: copy 1 frame RAM → SWAP | nội bộ |
| 4/5 `SYSMEM_IO_READ/WRITE` | đọc/ghi 1 byte vật lý (frame phải thuộc PID) | nội bộ |
| 6–9 `SYSMEM_REGION_ALLOC/FREE/READ/WRITE` | alloc/free/read/write theo region | nội bộ |
| 10 `SYSMEM_KMALLOC` | cấp phát kernel liên tục | nội bộ |
| 11/12 `SYSMEM_COPY_FROM/TO` | copy_from_user / copy_to_user | nội bộ |
| 13 `SYSMEM_DMA_OP` | gửi yêu cầu DMA | nội bộ |

Thêm syscall mới: tạo `src/sys_<tên>.c` với `int __sys_<tên>(struct krnl_t *, uint32_t pid, struct sc_regs *)`, thêm vào `SYSCALL_OBJ` (và `MM_SRC`, `MM_TEST_SRC`) trong Makefile, thêm dòng `<số> <tên> sys_<tên>` vào `src/syscall.tbl`.

---

## 7. Co-processor / DMA (Mục 2.3 của đề) — **đã làm**

- Dùng **DMA chip RTL thật qua Verilator** (`Vdma_top*`, không sửa). `ADDR_WIDTH = 64`, `DATA_WIDTH = 32`.
- `src/dma.c`: một kênh, một worker thread giữ model RTL; `dma_submit` nhận job rồi trả về ngay; worker chạy RTL rồi gọi callback hoàn tất.
- `src/sysbus.c`: RAM mô phỏng gắn làm bank 0 của bus (`memory_bank_attach`); callback DPI đọc/ghi 32-bit theo từng byte lane.
- `src/libmem.c` `mm_dma_map`: đổi 2 region thành các span vật lý, ghim frame cho tới khi DMA xong.
- Bất đồng bộ: lệnh `dma_memcpy` trả về ngay; lệnh bộ nhớ kế tiếp của cùng PID chờ (`-EBUSY`, thử lại slot sau); process chỉ kết thúc khi DMA xong.

---

## 8. Đồng bộ (Mục 2.4 của đề)

Thread: 1 loader, N CPU, 1 timer, 1 DMA worker.

| Tài nguyên | Khoá |
|---|---|
| Ready queue, `running_list`, `slot[]`, con trỏ queue trong `krnl` | `queue_lock` (`sched.c`) |
| Sổ đăng ký PID, VMA, symbol table, bảng trang, frame list RAM/SWAP, swap, mọi syscall | `mm_lock` (recursive, `mm64.c`) — `_syscall` giữ suốt handler |
| Byte RAM (CPU `MEMPHY_read/write` và DMA bus beat) | `ram_lock` (`sysbus.c`), luôn lấy sau cùng |
| Kênh DMA, job đang chạy | `lock` trong `dma.c` |
| Tiến trình timer | `event_lock` + condvar (`timer.c`) |

Thứ tự lấy khoá: `mm_lock` → khoá kênh DMA → `ram_lock`. Callback DMA chạy không giữ khoá kênh rồi mới lấy `mm_lock`. `queue_lock` không bao giờ được giữ cùng lúc với `mm_lock`.

---

## 9. Trạng thái code hiện tại (nhánh `tong_hop_code`, commit `baeccc6`)

| Module | Trạng thái | Nguồn |
|---|---|---|
| Scheduler MLQ (`queue.c`, `sched.c`) | Xong; đã sửa race ghi `krnl` ngoài khoá | nhánh `feat/schedular` |
| MM64 5 cấp, alloc/free/read/write, kmalloc, copy, sổ đăng ký PID (`mm64.c`, `mm-memphy.c`, `mm-vm.c`, `libmem.c`, `syscall.c`, `sys_mem.c`) | Xong | nhánh `2412009` (bản đầu tiên ở nhánh `2412017`, đã thay khi tích hợp) |
| Swap FIFO (`mm64.c`, `mm-memphy.c`) | Xong | tích hợp trên `tong_hop_code` |
| DMA driver (`dma.c`, `sysbus.c`, `dpi_rtl.cpp`, `dma_c_testbench.cpp`) | Xong | nhánh `feature/dma-verilator` |
| DMA mapper + async trong CPU (`libmem.c`, `cpu.c`, `os.c`, `sys_mem.c`) | Xong | tích hợp trên `tong_hop_code` |
| Syscall 440 (`sys_xxxhandler.c`) | Xong | tích hợp |
| Timer (`timer.c`): một mutex cho `done`, atomic | Xong | nhánh `2412009` |
| Test (`tests/test_mm.c`, `make test-dma`, `tests/run_teacher_tests.sh`, `tests/gantt.sh`) | 12/12 test-mm, 17/17 testbench DMA | — |
| Báo cáo | Chưa xong; có bản nháp phần bộ nhớ `docs/report_mm.tex` | — |

Kết quả chạy 17 config (`bash tests/run_teacher_tests.sh`): mọi config chạy hết, mọi process kết thúc; ASan/UBSan/LeakSanitizer và ThreadSanitizer không báo lỗi.

### 9.1 Giới hạn đã biết và việc còn lại

1. **Chưa chạy trên VM của môn**; mới kiểm tra trên Ubuntu WSL + Verilator 5.032.
2. Chính sách thay trang là FIFO; cần xác nhận với giảng viên (đề để giảng viên quyết định).
3. RAM < 4096 B (`os_1_mlq_paging_small_1K` và `os_syscall`, `os_sc` có RAM 2048 B) không có frame nào → alloc trong các config đó luôn thất bại; swap không giúp được.
4. Các `[INSTRUCTION-ERROR]` còn lại trong test của thầy đều do chính chương trình input (xem `docs/implementation_VI.md` mục 8.5): region chưa alloc, offset vượt region, syscall 101 không tồn tại.
5. FPN 13 bit → tối đa 8192 frame; kernel VA cấp kiểu bump (không tái dùng VA kernel đã free); không có TLB.
6. Chỉ một kênh DMA; DMA chỉ copy giữa 2 region của cùng một process.
7. Một số file (`libmem.c`, `mm64.c`, `mm-vm.c`, `os.c`) viết dồn nhiều lệnh trên một dòng, chưa theo GNU style; `sys_mem.c` và `sys_xxxhandler.c` đã theo GNU style.
8. Nhánh `#else` (không MLQ) của `sched.c` không được biên dịch và chưa được sửa.
9. `mm.c` (32-bit) không được bảo trì; tắt `MM64` sẽ không chạy đúng.

---

## 10. Báo cáo

- **Scheduling**: Gantt chart thứ tự process chạy trên từng CPU (`sh tests/gantt.sh <log>`).
- **Memory**: trạng thái cấp phát trong data segment (`[VMA]`, `[PAGE TABLE]`); chứng minh syscall giữa user và kernel dùng PID, không dùng PCB.
- **Multi-level paging**: sơ đồ dịch địa chỉ đa cấp, thống kê số lần truy cập bộ nhớ và dung lượng bảng trang (`[MM-STATS]`).
- Tự diễn giải kết quả mô phỏng.

Các câu hỏi phải trả lời (tất cả):

1. Ưu, nhược điểm của giao diện system call thống nhất cho nhiều thành phần (read/write/free cho file, bộ nhớ, thiết bị I/O): ảnh hưởng tới thiết kế OS, hiệu năng, độ phức tạp xử lý lỗi, cân bằng portability và hiệu quả.
2. Khi một system call chạy quá lâu, OS phát hiện và xử lý thế nào?
3. Với các chính sách MLQ chi tiết, lợi ích của từng chính sách là gì?
4. Động lực chính của việc kết hợp segmentation với paging? Cách tiếp cận lai giải quyết hạn chế của từng kỹ thuật ra sao?
5. Lợi ích của việc mở rộng phân trang phân cấp lên N cấp?
6. Ưu, nhược điểm của paging so với cấp phát liên tục?
7. Điều gì xảy ra nếu không xử lý đồng bộ? Minh hoạ bằng output của hệ thống (nếu có), đặc biệt với các thao tác kernel memory mới thêm.

Gợi ý trả lời có trong `README_MM_VI.md` mục 11.
