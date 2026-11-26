# Tài liệu hiện thực — Simple OS (demacOprx, CO2018 HK261)

Tài liệu mô tả cách nhóm hiện thực từng yêu cầu của đề `hk261_ossim_demacOprx.pdf`, nơi đặt code, cách kiểm chứng, và hướng dẫn chạy bộ test của thầy. Tóm tắt yêu cầu và trạng thái ngắn gọn nằm ở [../spec.md](../spec.md); giải thích sâu phần bộ nhớ nằm ở [../README_MM_VI.md](../README_MM_VI.md).

Trạng thái mô tả: nhánh `tong_hop_code`, commit `baeccc6` (27/11/2026).

## Mục lục

1. [Nguồn đóng góp](#1-nguồn-đóng-góp)
2. [Đối chiếu yêu cầu của đề](#2-đối-chiếu-yêu-cầu-của-đề)
3. [Kiến trúc tổng thể](#3-kiến-trúc-tổng-thể)
4. [Quản lý bộ nhớ](#4-quản-lý-bộ-nhớ)
5. [Scheduler MLQ](#5-scheduler-mlq)
6. [System call](#6-system-call)
7. [Co-processor DMA](#7-co-processor-dma)
8. [Hướng dẫn chạy test của thầy](#8-hướng-dẫn-chạy-test-của-thầy)
9. [Đồng bộ](#9-đồng-bộ)
10. [Kết quả kiểm thử hiện tại](#10-kết-quả-kiểm-thử-hiện-tại)
11. [Giới hạn và việc còn lại](#11-giới-hạn-và-việc-còn-lại)
12. [Gợi ý cho báo cáo](#12-gợi-ý-cho-báo-cáo)

---

## 1. Nguồn đóng góp

| Phần | Nhánh gốc | Tác giả commit (git log) | Trạng thái trong `tong_hop_code` |
|---|---|---|---|
| Scheduler MLQ: `queue.c`, `sched.c` | `feat/schedular` | Nguyen Ba Nhat | Dùng nguyên; khi tích hợp thêm `remove_running_proc` và sửa một data race |
| Bộ nhớ bản đầu: alloc/free/read/write, memphy, memmap, vm | `2412017` | ethantruman | Đã merge (PR #4), sau đó được thay bằng bản của `2412009` vì bản này đầy đủ hơn |
| Bộ nhớ MM64 đầy đủ, sổ đăng ký PID, kmalloc, copy_*_user, sửa timer, `tests/test_mm.c`, `README_MM_VI.md`, `docs/report_mm.tex` | `2412009` | (máy chưa cấu hình tên git) | Dùng làm nền phần bộ nhớ |
| DMA driver: `dma.c`, `sysbus.c`, `dpi_rtl.cpp`, `dma_c_testbench.cpp` | `feature/dma-verilator` | minhbk | Dùng nguyên |
| Tích hợp: swap FIFO, DMA mapper, DMA bất đồng bộ trong CPU, syscall 440, script test | `tong_hop_code` | ethantruman | Commit `4e4e8a4`, `baeccc6` |

---

## 2. Đối chiếu yêu cầu của đề

| Yêu cầu (mục của đề) | Điểm | Hiện thực | File chính | Kiểm chứng |
|---|---|---|---|---|
| Scheduler MLQ, slot = MAX_PRIO − prio (2.1, 3.2) | 2 | Mục 5 | `src/queue.c`, `src/sched.c` | `sched`, `sched_0`, `sched_1`, `os_1_singleCPU_mlq`; Gantt bằng `tests/gantt.sh` |
| Paging 64-bit, 5 cấp (2.2.2, 3.2 "Multi-level paging") | MMU 2 | Mục 4.1–4.3 | `src/mm64.c` | test-mm 01, 02, 08; `[PAGE TABLE]` |
| ALLOC/FREE/READ/WRITE theo Figure 7 (2.2.4) | MMU | Mục 4.4 | `src/libmem.c`, `src/mm-vm.c`, `src/mm-memphy.c` | test-mm 02–04; `os_0/1/2_*_paging` |
| Tách user/kernel, syscall truyền PID (3.2, 3.3) | MMU | Mục 4.5 | `src/syscall.c`, `src/sys_mem.c`, `src/libmem.c` | test-mm 05, 06; `os_mm_usr_demo` |
| KMALLOC liên tục vật lý, copy_from_user / copy_to_user (1.3, 2.2.1) | MMU | Mục 4.5 | `src/libmem.c` | test-mm 05; `os_mm_usr_demo`, `os_dma_demo` |
| Thay thế trang (3.2 "Optional page replacement") | — | Mục 4.6 (FIFO) | `src/mm64.c`, `src/mm-memphy.c` | test-mm 12; `os_swap_demo`, `os_1_mlq_paging_small_4K` |
| Thống kê truy cập bộ nhớ và dung lượng bảng trang (3.2) | Báo cáo | Mục 4.7 | `src/mm64.c` | `[MM-STATS]` |
| System call, thêm syscall (1.6) | — | Mục 6 | `src/syscall.*`, `src/sys_*.c` | `os_syscall_list`, `os_sc` |
| DMA bất đồng bộ (2.3, 3.2) | 1 | Mục 7.2 | `src/dma.c`, `src/cpu.c`, `src/os.c` | `os_dma_demo`, `os_2_mlq_paging` |
| Bus addressing: CPU qua MMU, co-processor truy cập bộ nhớ (3.4) | 2 | Mục 7.3 | `src/libmem.c` (`mm_dma_map`), `src/sysbus.c`, `src/dpi_rtl.cpp` | `os_dma_demo`, `make test-dma` |
| Dùng DMA chip RTL thật qua Verilator (3.2) | — | Mục 7.1 | `src/dpi_rtl.cpp` + `Vdma_top*` | `make all`, `make test-dma` |
| Đồng bộ tài nguyên dùng chung (2.4) | — | Mục 9 | toàn bộ | ThreadSanitizer, chạy lặp |

---

## 3. Kiến trúc tổng thể

### 3.1 Các thread

```text
main ─┬─ timer thread      (timer.c)   phát time slot
      ├─ loader thread     (os.c ld_routine)   nạp process đúng thời điểm, add_proc
      ├─ CPU thread × N    (os.c cpu_routine)  get_proc → run() từng lệnh → put_proc
      └─ DMA worker        (dma.c)     giữ model RTL, chạy từng job DMA
```

`main` khởi tạo RAM và SWAP (`init_memphy`), gắn RAM vào system bus (`memory_bank_attach`), đăng ký DMA mapper (`dma_set_mapper(mm_dma_map)`) và khởi động DMA worker (`dma_start`) trước khi tạo thread loader và các thread CPU (thread timer đã chạy từ trước đó). Khi mọi CPU dừng, nó gọi `dma_stop`, in `kernel-final` rồi giải phóng bộ nhớ.

### 3.2 Đường đi của một lệnh bộ nhớ

```text
CPU run(proc)                              (user space)
  └─ liballoc / libread / libwrite / libkmem_* / libmem_dma_memcpy   (libmem.c)
       └─ _syscall(krnl, pid, 17, regs)    chỉ PID + giá trị, cờ SC_INTERNAL
────────────────────────────────────────── (kernel space)
            └─ mm_lock; mm_lookup(krnl, pid) → PCB thật
               └─ __sys_memmap (sys_mem.c) → __alloc / __read / __write / __kmalloc /
                                              mm_copy_region / dma_copy_regions
                    ├─ mm-vm.c      VMA, free region, inc_vma_limit
                    ├─ mm64.c       walk 5 cấp, map/unmap, translate, swap
                    └─ mm-memphy.c  frame RAM/SWAP, MEMPHY_read/write
```

---

## 4. Quản lý bộ nhớ

### 4.1 Không gian địa chỉ

| Vùng | Địa chỉ | Cây bảng trang | `mode_bit` |
|---|---|---|---|
| User (heap/data, VMA 0) | từ `0x0000000000000000`, tăng theo `sbrk` | riêng mỗi process: `proc->mm->pgd` | 1 |
| Không canonical | `0x0100000000000000` … `0xfeffffffffffffff` | — | — |
| Kernel | từ `kernel_base` (mặc định `0xff00000000000000`, đổi được bằng dòng tuỳ chọn trong config) | dùng chung: `krnl->mm->pgd` | 0 |

Địa chỉ canonical: bit 63–57 bằng bit 56 (`mm_canonical`). Kernel space giống nhau ở mọi process vì chỉ có một cây kernel.

### 4.2 Bảng trang 5 cấp

- Chỉ số mỗi cấp lấy bằng `get_pd_from_address`: PGD = bit 56–48, P4D = 47–39, PUD = 38–30, PMD = 29–21, PT = 20–12, offset = 11–0. Ví dụ của đề `0x39ffe1d9c9000` → PGD 0x003, P4D 0x13f, PUD 0x1f8, PMD 0x0ec, PT 0x1c9 (test-mm 01 kiểm tra đúng 5 chỉ số này).
- `walk(mm, va, create, count)` đi từ PGD xuống PT. 4 cấp trên chứa con trỏ tới bảng con; cấp cuối chứa PTE. Khi `create = 1` và nhánh chưa có, `new_table` cấp một bảng 512 × 8 byte (cấp phát theo nhu cầu).
- `prune` giải phóng các bảng con rỗng sau khi unmap; `destroy_table` duyệt đệ quy chỉ những nhánh đã tồn tại khi process kết thúc.
- `mm_translate(mm, va, &pa, write)`: walk, kiểm tra present, ghép `FPN × 4096 + offset`, đặt bit dirty khi ghi.

### 4.3 PTE

PTE 32 bit theo Figure 6 của đề, lưu trong slot 64 bit (`init_pte`, `pte_set_fpn`, `pte_set_swap`, `pte_get_entry`, `pte_set_entry`).

- Present: bit 31 = 1, bit 0–12 = FPN, bit 28 = dirty.
- Swapped: bit 31 = 0, bit 30 = 1, bit 0–4 = id thiết bị SWAP, bit 5–25 = slot trong SWAP.
- FPN 0 là hợp lệ: bit present mới cho biết có mapping.
- FPN chỉ có 13 bit nên tối đa 8192 frame; frame vượt giới hạn được coi là không dùng được.

### 4.4 ALLOC / FREE / READ / WRITE (Figure 7)

**ALLOC `alloc size rg`** (`__alloc`):
1. Kiểm tra `rg` trong 0..9 và region chưa được dùng.
2. First-fit trong `vm_freerg_list` (`get_free_vmrg_area`).
3. Không đủ chỗ → `SYSMEM_INC_OP` (`inc_vma_limit`) chỉ xin thêm phần còn thiếu (làm tròn lên page), map frame qua `vm_map_ram` → `mm_map_pages`, rồi đưa vùng mới vào free list và lấy lại first-fit.
4. Xoá trắng vùng được cấp (không lộ dữ liệu cũ), ghi `symrgtbl[rg]` và `proc->regs[rg]`, in `[ALLOC]` và `[VMA]`.
5. Hết RAM giữa chừng → rollback: trả frame đã lấy, không đổi `sbrk`/symbol.

**FREE `free rg`** (`__free`):
- Region user: đưa về free list, sắp theo địa chỉ và gộp vùng liền kề (`vm_insert_free`). Frame giữ lại vì một page có thể chứa nhiều region.
- Region kernel: unmap và trả frame ngay.

**READ/WRITE** (`access_byte`):
1. Kiểm tra region còn sống, đúng `mode_bit = 1`, `offset < size` (khoảng nửa mở).
2. `mm_translate_fault` dịch VA → PA; nếu page đang ở SWAP thì swap in (mục 4.6).
3. Kiểm tra frame thuộc đúng `mm` (`MEMPHY_owned`).
4. Đọc/ghi qua `SYSMEM_IO_READ` / `SYSMEM_IO_WRITE` → `MEMPHY_read` / `MEMPHY_write`.

Lệnh `read rg off dst` lưu byte đọc được vào `proc->regs[dst]`. Nếu `dst ≥ 10` (ví dụ `read 1 20 20` trong `p0s`), phép đọc vẫn thực hiện nhưng chỉ in cảnh báo vì CPU chỉ có 10 thanh ghi.

### 4.5 Tách user space / kernel space

| Cơ chế | Hiện thực |
|---|---|
| Không truyền PCB qua ranh giới | Wrapper gọi `_syscall(krnl, pid, …)`; `_syscall` giữ `mm_lock`, tìm PCB bằng `mm_lookup(krnl, pid)` trong sổ đăng ký `krnl->processes`. PID không có → `-ESRCH` |
| Phân biệt wrapper tin cậy | Memop 3–13 yêu cầu `regs.flags & SC_INTERNAL`; `libsyscall` (lệnh `syscall` của user) không đặt cờ này nên `syscall 17 4 …` bị `-EPERM` |
| Quyền truy cập region | `mode_bit`: READ/WRITE chỉ cho region user; region kernel chỉ qua copy |
| KMALLOC | `__kmalloc` tìm dãy frame liên tiếp (`MEMPHY_get_contiguous`), map vào cây kernel dùng chung, VA kernel cấp tăng dần; descriptor region nằm trong symbol table của process chủ nên process khác không dùng được |
| COPY_FROM_USER / COPY_TO_USER | `mm_copy_region`: kiểm tra toàn bộ khoảng ở cả 2 region (src user → dst kernel, hoặc ngược lại), rồi copy từng byte cùng offset, kể cả qua biên page |
| Vòng đời PCB | Khi process kết thúc: `remove_running_proc` → `mm_unregister` → `free_pcb_memph` (thu hồi region kernel, frame RAM, slot SWAP, cây bảng trang) → `free` PCB. Dispatcher giữ `mm_lock` từ lúc lookup đến hết handler nên không dùng PCB đã free |

### 4.6 Thay thế trang (swap) — FIFO

Đề để giảng viên quyết định yêu cầu thay trang; nhóm hiện thực FIFO toàn hệ thống.

**Dữ liệu:** mỗi frame RAM đang dùng có reverse mapping `owner` (mm), `pgn` (page ảo), `stamp` (thời điểm page vào RAM, tăng đơn điệu), `pin` (DMA đang dùng). Ghi bằng `MEMPHY_track` mỗi khi map page hoặc swap in.

**Chọn victim** (`MEMPHY_pick_victim`): frame có `stamp` nhỏ nhất, bỏ qua frame của kernel (`owner == krnl->mm`) và frame đang được ghim (`pin > 0`).

**Swap out** (`swap_out_one` trong `mm64.c`):
1. Lấy slot trống trong SWAP đang hoạt động (`MEMPHY_get_freefp`).
2. Copy 4096 byte RAM → SWAP (`__swap_cp_page`).
3. Ghi PTE của victim thành swapped (type = `active_mswp_id`, offset = slot).
4. Trả frame RAM, tăng `swap_outs` của mm chủ, in `[SWAP-OUT]`.

**Khi nào swap out:** `get_user_frame` không lấy được frame RAM (hoặc frame vượt giới hạn 13 bit FPN). Hàm này được dùng khi map page user mới và khi swap in.

**Page fault / swap in** (`mm_translate_fault`): mọi chỗ truy cập dữ liệu đều dịch địa chỉ bằng hàm này, gồm READ/WRITE, COPY, zero-fill của ALLOC, `SYSMEM_MAP_OP` và DMA mapper. Nếu PTE là swapped thì:
1. Lấy frame (có thể đẩy page khác ra).
2. Copy slot về RAM.
3. Trả slot, đặt PTE present.
4. Tăng `swap_ins`, in `[SWAP-IN]`.

**Dọn dẹp:** `mm_unmap_pages` và `destroy_table` trả cả frame RAM lẫn slot SWAP, nên không rò slot.

Ví dụ `./os os_swap_demo` (RAM 2 frame, 2 process mỗi process dùng 3 page):

```text
[SWAP-OUT] PID=1 VA=0000000000000000 FPN=0 -> SWP0 slot=0
[SWAP-IN]  PID=1 VA=0000000000000000 SWP0 slot=0 -> FPN=1
[READ] PID=1 reg=0 offset=0 value=65
...
[READ] PID=2 reg=1 offset=0 value=67
```

### 4.7 Thống kê cho báo cáo

`[MM-STATS] PID=<pid> <space> PGD= P4D= PUD= PMD= PT= bytes= peak_bytes= walks= pt_access= data_access= total= swap_in= swap_out=`

| Trường | Ý nghĩa |
|---|---|
| `PGD`…`PT` | số bảng đang có ở từng cấp |
| `bytes`, `peak_bytes` | dung lượng bảng trang hiện tại / lớn nhất (mỗi bảng 4096 B) |
| `walks` | số lần dịch địa chỉ |
| `pt_access` | số lần đọc entry bảng trang khi dịch (5 cho mỗi walk đầy đủ) |
| `data_access` | số byte dữ liệu được đọc/ghi hoặc xoá trắng |
| `swap_in`, `swap_out` | số page được đưa về / đẩy ra |

Thời điểm in: sau `kmalloc` (`kernel-shared`), khi process kết thúc (`user`, `user-final`), khi tắt máy (`kernel-final`). Ví dụ `os_mm_usr_demo`: mỗi process có `walks=18 pt_access=90` (đúng 5 truy cập mỗi lần dịch), `bytes=20480` (một nhánh đầy đủ 5 bảng × 4 KB), so với 128 TiB nếu dùng bảng phẳng cho 2^45 page.

---

## 5. Scheduler MLQ

**Cấu trúc:**
- `queue_t` là mảng tối đa 50 PCB.
- `enqueue` thêm vào cuối.
- `dequeue` lấy phần tử có `prio` nhỏ nhất, nếu bằng nhau thì lấy phần tử vào trước. Trong MLQ mỗi hàng đợi chỉ có một mức `prio`, nên thực tế là FIFO.
- `purgequeue` gỡ một PCB cụ thể.

**Thuật toán** (`get_mlq_proc`, giữ `queue_lock`):
1. Duyệt `prio` từ 0 đến 139, tìm hàng đợi đầu tiên vừa còn process vừa còn `slot[prio] > 0`. Lấy 1 process ra và trừ 1 slot.
2. Nếu có hàng đợi còn process nhưng tất cả đều hết slot → nạp lại `slot[i] = MAX_PRIO − i` rồi duyệt lại.
3. Thêm process vào `running_list`.

**Vòng CPU** (`cpu_routine`):
- Mỗi process chạy tối đa `time_slice` lệnh rồi `put_proc` đưa về hàng đợi theo `prio` (gỡ khỏi `running_list`).
- Process chạy hết lệnh (và không còn DMA đang chạy) thì được thu hồi.
- Hết process và loader đã nạp xong thì CPU dừng.

**Dual priority:** `prio` lấy từ file config (live priority), ghi đè priority trong file chương trình.

**So với output mẫu:**
- CPU nào nhận process trước phụ thuộc thứ tự thread nên có thể khác mẫu.
- Một số output mẫu không khớp với số lệnh của chương trình. Ví dụ ở `sched`, `p3s` có 11 lệnh và time slice 4, nên phải xong sau 4 + 4 + 3 slot, trong khi mẫu cho chạy khoảng 17 slot.
- Dòng `Time slot N` do thread timer in song song với CPU, nên ranh giới slot trong log có thể lệch ±1.

---

## 6. System call

- Bảng: 0 `listsyscall`, 17 `memmap`, 440 `xxx` (ví dụ mục 1.6.3: in `The first system call parameter <a1>`). Số khác → `-ENOSYS`.
- `memmap` gồm 5 memop công khai của đề (MAP, INC, SWP, IO_READ, IO_WRITE) và các memop nội bộ 6–13. Bảng đầy đủ ở `spec.md` mục 6.
- `src/sys_mem.c` và `src/sys_xxxhandler.c` viết theo GNU C style như mục 3.1 của đề yêu cầu.
- Thêm syscall mới: tạo `src/sys_<tên>.c`, thêm `sys_<tên>.o` vào `SYSCALL_OBJ` (và `sys_<tên>` vào `MM_SRC`, `MM_TEST_SRC`) trong Makefile, thêm dòng vào `src/syscall.tbl`, rồi `make all`.

---

## 7. Co-processor DMA

### 7.1 Thành phần

| File | Vai trò |
|---|---|
| `src/dpi_rtl.cpp` | Điều khiển model Verilator `Vdma_top`: reset, nạp descriptor (`desc_data_0/1/2` = src, dst, số byte), đếm clock đến khi `ch_done`; `dma_done_callback` được RTL gọi qua DPI khi xong |
| `src/sysbus.c` | System bus: RAM mô phỏng là bank 0; `dpi_bank_read32_legacy` / `dpi_bank_write32_legacy` đọc/ghi 32 bit theo từng byte lane (byte-by-byte, tránh lỗi endian), chặn truy cập ngoài descriptor; `sysbus_lock_ram` |
| `src/dma.c` | Một kênh DMA, một worker thread; `dma_submit` (nhận job, trả `-EBUSY` nếu kênh bận), `dma_pending(pid)`, `dma_copy_regions` (gọi mapper rồi submit) |
| `src/libmem.c` | `libmem_dma_memcpy` (gửi `SYSMEM_DMA_OP`) và `mm_dma_map` (mapper phía bộ nhớ) |
| `src/cpu.c`, `src/os.c` | Chờ DMA trước lệnh bộ nhớ kế tiếp; chỉ kết thúc process khi DMA xong; khởi động/dừng DMA |

### 7.2 Bất đồng bộ

```text
CPU: dma_memcpy 0 1 0 4 → syscall 17 SYSMEM_DMA_OP → mm_dma_map → dma_submit → trả về 0 ngay
CPU: calc, calc          ← vẫn chạy trong lúc DMA copy
CPU: read 1 0 5          ← dma_pending(pid) → -EBUSY, thử lại ở slot sau
DMA worker: dpi_rtl_dma_copy → RTL → ch_done → dma_release (gỡ ghim) → [DMA-DONE]
CPU: read 1 0 5          ← chạy được, đọc đúng dữ liệu đã copy
```

- Lệnh CALC của cùng PID và mọi lệnh của PID khác vẫn chạy trong lúc DMA làm việc.
- Kênh bận với PID khác → `dma_memcpy` lùi `pc` và thử lại sau.
- Process chỉ được thu hồi khi `dma_pending(pid) == 0`.

### 7.3 Bus addressing

- **CPU truy cập qua MMU:** VA → walk 5 cấp → PA → `MEMPHY_read/write`.
- **Co-processor truy cập bộ nhớ:** `mm_dma_map` dịch cả hai region và trả về danh sách span vật lý `{src_pa, dst_pa, bytes}`. Span được cắt tại mọi biên page của cả hai phía (vì frame của một region không liên tục), và các span liền nhau được gộp lại. RTL đọc/ghi thẳng các địa chỉ vật lý đó trên bank 0 của bus.
- **Ghim frame:** mapper `MEMPHY_pin(+1)` từng frame sau khi dịch (page nguồn được ghim trước khi fault page đích), nên swap không đẩy các frame đó ra. Callback hoàn tất `dma_release` gỡ ghim dưới `mm_lock`.
- Byte RAM do CPU và DMA cùng truy cập được bảo vệ bằng `ram_lock` của bus.

`DMA_TRACE=1 ./os os_dma_demo` in từng span trên bus và ghi waveform `rtl_trace.vcd`.

---

## 8. Hướng dẫn chạy test của thầy

### 8.1 Chuẩn bị môi trường (Windows → WSL Ubuntu)

Làm một lần, trong PowerShell:

```powershell
wsl --install -d Ubuntu      # lần đầu mở sẽ hỏi tạo username/password
```

Trong Ubuntu:

```sh
sudo apt-get update
sudo apt-get install -y build-essential verilator git
```

Nên làm việc trong ổ Linux (ví dụ `~/ossim`) thay vì `/mnt/c/...`: build nhanh hơn và không bị lỗi CRLF.

```sh
git clone -b tong_hop_code /mnt/c/Users/<user>/SIMPLE_OS_CO2017 ~/ossim
cd ~/ossim
```

Nếu sửa code bên Windows, mỗi lần muốn test thì `git pull` lại trong `~/ossim`. Lưu ý chỉ những thay đổi đã commit mới được pull về.

### 8.2 Build

```sh
make clean
make all          # ./os đầy đủ có DMA RTL
make os-mm        # ./os-mm không cần Verilator (DMA_MEMCPY trả -ENOSYS)
```

Cảnh báo `unused parameter` từ `src/mem.c` (code legacy) là bình thường.

### 8.3 Chạy từng test

```sh
./os sched_0                    # tên file trong input/, không ghi "input/"
./os os_1_mlq_paging > out.txt  # lưu log
```

### 8.4 Chạy toàn bộ và đọc kết quả

```sh
bash tests/run_teacher_tests.sh              # tất cả config trong input/
bash tests/run_teacher_tests.sh sched sched_0
BIN=./os-mm bash tests/run_teacher_tests.sh  # bản không DMA
make test-mm                                 # 12 nhóm test assert
make test-dma                                # testbench RTL của DMA
sh tests/gantt.sh tests/results/sched.log    # Gantt chart
```

Script in bảng `CONFIG RC PROCESS FINISHED ERRORS SWAPS DMA_DONE`, sau đó liệt kê từng `[INSTRUCTION-ERROR]` kèm lệnh gốc trong file chương trình. Log đầy đủ lưu ở `tests/results/<config>.log`.

Một lần chạy được coi là đạt khi:
- `RC = 0` và `FINISHED = PROCESS`;
- các lỗi lệnh (nếu có) khớp với bảng 8.5;
- thứ tự lập lịch giải thích được theo mục 5.

Không bắt buộc khớp từng dòng với `output/`.

**Các tag trong log:**

| Tag | Ý nghĩa |
|---|---|
| `[MM-CONFIG]` | RAM, page size, số frame dùng được, kernel base |
| `[ALLOC]` / `[KMALLOC]` | VA được cấp cho region |
| `[VMA]` | VMA, các region đang dùng và vùng trống |
| `[READ]` | giá trị đọc được |
| `[PAGE TABLE]` | các PTE (VA, PTE, FPN hoặc SWAPPED SWPx slot) khi process kết thúc |
| `[MM-STATS]` | thống kê (mục 4.7) |
| `[SWAP-OUT]` / `[SWAP-IN]` | thay thế trang |
| `[DMA-SUBMIT]` / `[DMA-DONE]` | gửi và hoàn tất DMA |
| `[INSTRUCTION-ERROR]` | lệnh thất bại (PID, PC, opcode, mã lỗi) |

### 8.5 Từng config: nội dung và kết quả mong đợi

| Config | CPU / process / RAM | Kiểm tra gì | Lỗi lệnh mong đợi và lý do |
|---|---|---|---|
| `sched` | 2 / 4 / mặc định | MLQ 2 CPU | 0 |
| `sched_0` | 1 / 4 / mặc định | MLQ 1 CPU | 0 |
| `sched_1` | 1 / 5 / mặc định | MLQ 1 CPU | 0 |
| `os_1_singleCPU_mlq` | 1 / 8 / mặc định (config kiểu cũ) | MLQ + bộ nhớ | 1: `m0s write 1 2 1000` (offset 1000 vượt region 100 byte) |
| `os_1_singleCPU_mlq_paging` | 1 / 8 / 1 MiB | như trên | 1: như trên |
| `os_0_mlq_paging` | 2 / 5 / 256 MiB | alloc/free/read/write | 8: `p0s` ghi/đọc region 2, 3 chưa alloc (mỗi process `p0s` 4 lỗi) |
| `os_1_mlq_paging` | 4 / 8 / 256 MiB | như trên, 4 CPU | 5: `p0s` như trên + `m0s write 1 2 1000` |
| `os_1_mlq_paging_small_4K` | 4 / 8 / 4096 B (1 frame) | **swap** | 5 (như `os_1_mlq_paging`); có `[SWAP-*]` |
| `os_1_mlq_paging_small_1K` | 4 / 8 / 2048 B | RAM nhỏ hơn 1 page | 23: mọi alloc thất bại vì không có frame 4 KB nào |
| `os_2_mlq_paging` | 4 / 6 / 1 MiB | kmalloc, copy, **DMA** | 6: `m2s` copy vào region 4 trước khi `kmalloc 4`, copy từ region 3 chưa cấp, `free 3` (2 process × 3); `DMA_DONE = 2` |
| `os_2_singleCPU_mlq_paging` | 1 / 5 / 1 MiB | như trên, 1 CPU | 6, `DMA_DONE = 2` |
| `os_syscall_list` | 1 / 1 / 2048 B | syscall 0 | 0; in thêm `440-sys_xxxhandler` so với mẫu |
| `os_sc` | 1 / 1 / 2048 B | syscall 440 | 0; in `The first system call parameter 1` |
| `os_syscall` | 1 / 1 / 2048 B | syscall 101 | 5: RAM < 1 page nên `alloc` và 3 `write` thất bại; syscall 101 không tồn tại (`-38`) |

Test do nhóm thêm (luôn 0 lỗi):

| Config | Kiểm tra gì | Kết quả đúng |
|---|---|---|
| `os_mm_usr_demo` | kernel base tuỳ chọn, kmalloc, copy_from/to_user, tái sử dụng sau free | mỗi PID đọc 165, 254, rồi 0 |
| `os_dma_demo` | DMA trong 1 page, qua biên page, user → kernel; 2 process tranh 1 kênh | mỗi PID đọc 11, 44, 77, 88, 33; `DMA_DONE = 6` |
| `os_swap_demo` | RAM 2 frame, 2 process | mỗi PID đọc 65, 66, 67 sau nhiều lần swap |

### 8.6 Kiểm tra lỗi bộ nhớ và race (tuỳ chọn)

```sh
make test-mm MM_TEST_FLAGS='-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer'

# ThreadSanitizer cho bản đầy đủ (nên làm ở một bản copy riêng)
make clean
make all CFLAGS='-std=gnu11 -c -g -O1 -fsanitize=thread' \
         CXXFLAGS='-std=gnu++14 -c -g -O1 -fsanitize=thread' \
         LFLAGS='-Wall -g -fsanitize=thread'
setarch $(uname -m) -R ./os os_dma_demo
```

---

## 9. Đồng bộ

| Tài nguyên dùng chung | Khoá | Ghi chú |
|---|---|---|
| Ready queue, `running_list`, `slot[]`, con trỏ queue trong `krnl` | `queue_lock` (`sched.c`) | |
| Sổ đăng ký PID, VMA, symbol table, bảng trang, frame RAM/SWAP, swap | `mm_lock` (`mm64.c`, recursive) | `_syscall` giữ từ lúc tìm PID đến hết handler; recursive vì handler lại gọi syscall lồng (INC, IO) |
| Byte RAM giữa CPU và DMA | `ram_lock` (`sysbus.c`) | luôn lấy sau cùng |
| Kênh và job DMA | `lock` (`dma.c`) | callback hoàn tất chạy không giữ khoá này |
| Time slot | `event_lock` + condvar (`timer.c`) | bản sửa của `2412009` dùng một mutex cho biến `done` |

- **Thứ tự lấy khoá:** `mm_lock` → khoá DMA → `ram_lock`. `queue_lock` không bao giờ giữ cùng `mm_lock`, nên không có deadlock vòng.
- **Ví dụ race đã gặp và đã sửa:** ThreadSanitizer báo `put_mlq_proc`/`add_mlq_proc` (nhánh `feat/schedular`) ghi `proc->krnl->ready_queue/...` (biến toàn cục `os`) ngoài `queue_lock` từ nhiều CPU cùng lúc. Đã chuyển vào trong khoá.
- **Ví dụ minh hoạ cho câu hỏi đồng bộ:** nếu bỏ `mm_lock`, hai CPU có thể cùng lấy một frame đầu free list và map cho hai process khác nhau. Khi đó process này đọc được dữ liệu của process kia, và free list bị hỏng (test-mm 11 chạy 4 thread × 100 vòng để kiểm tra điều này không xảy ra).

---

## 10. Kết quả kiểm thử hiện tại

Môi trường: Ubuntu 26.04 (WSL2), gcc 15.2, GNU Make 4.4.1, Verilator 5.032.

| Kiểm tra | Kết quả |
|---|---|
| `make all`, `make os-mm`, `make dma` | build thành công |
| `make test-mm` | 12/12 nhóm pass (cả khi bật ASan/UBSan) |
| `make test-dma` | 17 PASS, 0 FAIL |
| `bash tests/run_teacher_tests.sh` (17 config) | mọi config `RC = 0`, mọi process kết thúc; số lỗi lệnh đúng như bảng 8.5 |
| ASan + UBSan + LeakSanitizer trên `os-mm`, 17 config | 0 báo cáo |
| ThreadSanitizer trên `os`, 17 config | 0 cảnh báo (sau khi sửa race scheduler) |
| Chạy lặp 15 lần × 7 config nhiều CPU | 0 crash/treo, giá trị demo luôn đúng, số lỗi lệnh không đổi |

---

## 11. Giới hạn và việc còn lại

1. **Chưa chạy trên VM của môn.** Cần build và chạy `bash tests/run_teacher_tests.sh` ở đó trước khi demo.
2. **Chính sách thay trang FIFO** chưa được giảng viên xác nhận. Nếu cần chính sách khác, chỉ phải sửa `MEMPHY_pick_victim`.
3. **RAM < 4096 B không có frame nào** (page 4 KB của MM64), nên swap không cứu được các config RAM 2048 B.
4. **Giới hạn của thiết kế:** FPN 13 bit (8192 frame); VA kernel cấp kiểu bump, không tái dùng; không có TLB; một kênh DMA; DMA chỉ giữa 2 region của cùng một process.
5. **Coding style:** nhiều file bộ nhớ (`libmem.c`, `mm64.c`, `mm-vm.c`, `os.c`) viết dồn nhiều lệnh trên một dòng. Nên format lại trước khi nộp, ít nhất là các phần liên quan đến syscall.
6. **Output khác mẫu:** có thêm dòng log và syscall 440 trong `listsyscall`. Cần giải thích trong báo cáo.
7. **Code không được bảo trì:** nhánh `#else` (không MLQ) của `sched.c` và `mm.c` (32-bit).
8. **Báo cáo chưa xong:** `docs/report_mm.tex` mới có phần bộ nhớ; cần thêm scheduler, DMA, swap và kết quả đo.

---

## 12. Gợi ý cho báo cáo

| Mục báo cáo (3.3) | Lấy từ đâu |
|---|---|
| Gantt chart | `./os sched > s.log; sh tests/gantt.sh s.log` (làm tương tự cho `sched_0`, `sched_1`, `os_1_singleCPU_mlq`) |
| Trạng thái cấp phát data segment | `[ALLOC]`, `[VMA]`, `[PAGE TABLE]` trong `os_0_mlq_paging`, `os_mm_usr_demo` |
| Syscall truyền PID, không truyền PCB | mục 4.5; `src/libmem.c` (`memcall`, `region_call`), `src/syscall.c` (`_syscall`, `mm_lookup`) |
| Sơ đồ dịch địa chỉ đa cấp | mục 4.2, ví dụ `0x39ffe1d9c9000`; `[PAGE TABLE]` |
| Số lần truy cập bộ nhớ, dung lượng bảng trang | `[MM-STATS]` (mục 4.7); so sánh cây 5 cấp với bảng phẳng |
| Swap | `os_swap_demo`, `os_1_mlq_paging_small_4K` (`[SWAP-*]`, `swap_in/out`) |
| DMA bất đồng bộ và bus addressing | `os_dma_demo` (thứ tự `[DMA-SUBMIT]` → lệnh khác → `[DMA-DONE]`), `DMA_TRACE=1`, `rtl_trace.vcd` |
| Câu hỏi của đề | `spec.md` mục 10; gợi ý trả lời trong `README_MM_VI.md` mục 11 |
