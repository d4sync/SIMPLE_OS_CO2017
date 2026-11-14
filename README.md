# demacOprx - OS Simulation for CO2018

Operating System Simulation with DMA (Verilator RTL) for HCMC University of Technology Course CO2018.

## Build full OS simulation - The output file ./os

```bash
make os    # Build full OS simulation. The output file ./os
```
Run os simulation
```bash
# OS simulation
./os [configuration_file]
```

## Build only DMA module - The output file dma_testbench

```bash
make dma 
```
Run DMA module:
```bash
./dma_testbench
./dma_testbench 0x1000 0x2000 1024
```
## demacOprx environment
Ubuntu Virtual Machine image for Intel-x86 and AARCH64 (required HCMUT.SSO system)
[Course CO2018 Documentation](https://docs.google.com/document/d/1vCxl0sBotFjONqRJBAlnkTVYtlL1XZQrTyVl_dO0zxg/edit?usp=sharing "Course CO2018 Documentation - HCMC University of Technology")

## License

See [LICENSE](LICENSE) — educational use only for CO2018.

---
---

# PHẦN CỦA NHÓM — Kế hoạch 15 ngày

> **Hôm nay: 14/11/2026 · Deadline (giả định): 29/11/2026** (15 ngày). *Cần xác nhận lại giờ nộp chính xác trên LMS.*
> Tài liệu liên quan: [`spec.md`](./spec.md) (đặc tả diễn giải, nguồn sự thật) · [`AGENTS.md`](./AGENTS.md) (quy tắc cho AI agent) · PDF đặc tả gốc `hk261_ossim_demacOprx.pdf` (ký hiệu **p.X** = trang trong PDF).

## A. Cách dùng tài liệu này (để không bị loạn)

Đặc tả dài 33 trang, **không ai cần đọc hết**. Làm theo thứ tự:

1. **Cả nhóm** đọc phần *"Đọc chung"* (mục B) — khoảng 30 phút.
2. **Mỗi người** chỉ đọc kỹ phần của mình trong mục D (có ghi rõ mục trong `spec.md` và trang PDF), cộng phần "tự tìm hiểu thêm".
3. Phần của người khác chỉ cần đọc **"Giao diện giữa các module"** (mục E) để biết mình phụ thuộc/cung cấp gì.
4. Gặp điểm không rõ → ghi vào **`docs/questions.md`** và hỏi nhóm/thầy; **không tự đoán** (xem `AGENTS.md`).

## B. Đọc chung (cả 4 người, trước khi code)

| Đọc gì | Ở đâu | Vì sao |
|---|---|---|
| Tổng quan kiến trúc | `spec.md §1` · PDF p.3–4 | Biết 3 module và mối quan hệ |
| Process, PCB, tập lệnh, file chương trình | `spec.md §3` · PDF p.5–7 | Mọi module đều xử lý process/lệnh |
| Cách chạy mô phỏng, file cấu hình, `os-cfg.h`, `krnl_t` | `spec.md §4` · PDF p.7–8, p.20–21 | Cần để chạy/test |
| Yêu cầu nộp, báo cáo, chấm điểm | `spec.md §11` · PDF p.26–27 | Biết điểm nằm ở đâu |
| Quy tắc agent + điểm chưa rõ | `AGENTS.md`, `spec.md §13` | Tránh làm sai hướng |

Việc cả nhóm làm **ngày 1**: clone repo, `make os`, `make dma`, chạy `./os` với 1–2 file trong `input/`, chạy `./dma_testbench`, mở thử các file trong `src/` để biết code nằm ở đâu.

## C. Phân công 4 người

| | Vai trò | Deliverable chính | Điểm demo liên quan | Câu hỏi báo cáo |
|---|---|---|---|---|
| **SV1** | **Scheduler + Synchronization** | `enqueue`/`dequeue` (`queue.c`), `get_proc` (`sched.c`), khoá toàn hệ thống, Gantt | Scheduling (2đ) | Q3, Q7 |
| **SV2** | **Paging 32-bit + Swap** | `libmem.c`, `mm-vm.c`, `mm-memphy.c`, `mm.c`, syscall `memmap` | MMU (2đ) | Q4, Q6 |
| **SV3** | **64-bit paging + User/Kernel + Syscall** | `mm64.c`, `kmalloc`, `copy_from/to_user`, PID passing, thống kê | MMU & user/kernel (2đ, chung với SV2) | Q1, Q2, Q5 |
| **SV4** | **Co-processor (DMA) + Tích hợp + Báo cáo** | DMA verilator + async + `dma_memcpy`, test tích hợp, tổng hợp báo cáo & đóng gói zip | Co-processor (3đ) | — (tổng hợp) |

Nguyên tắc: ai viết code module nào thì viết phần báo cáo module đó và **phải giải thích được từng dòng** khi vấn đáp.

## D. Việc của từng người — đọc gì, tìm hiểu thêm gì

### SV1 — Scheduler + Synchronization

**Việc cần làm**
1. Cài `enqueue()` và `dequeue()` trong `queue.c` (queue chứa PCB).
2. Cài `get_proc()` trong `sched.c` theo MLQ: `slot = MAX_PRIO - prio`, hết slot thì sang queue kế, phần còn lại để lượt sau; round-robin trong queue; process hết time slice quay về **đúng queue** của nó.
3. Đảm bảo dual priority (priority trong file cấu hình ghi đè priority file chương trình).
4. Xác định **mọi tài nguyên dùng chung** và đặt lock (làm cùng SV2/SV3/SV4 ở giai đoạn tích hợp).
5. Vẽ Gantt, trả lời Q3, Q7 (có ví dụ từ output thật).

**Đọc kỹ**
- `spec.md §6` (Scheduler) · PDF **p.12–13** (§2.1)
- `spec.md §9` (Đồng bộ) · PDF **p.24** (§2.4)
- `spec.md §3.2` + `§4.1` (priority, file cấu hình) · PDF p.7
- Code: `queue.h/queue.c`, `sched.h/sched.c`, `cpu.c`, `loader.c`, `os.c` (xem luồng loader → ready_queue → CPU), `input/` + `output/` mẫu có `MLQ`.

**Tự tìm hiểu thêm**
- Thuật toán **Multilevel Queue** và so sánh với MLFQ; Round Robin; starvation.
- **pthread mutex / spinlock**, race condition, deadlock (thứ tự lấy lock).
- Cách vẽ Gantt từ log của `os` (viết script nhỏ parse output).
- Công cụ: `-fsanitize=thread`, `valgrind --tool=helgrind`.

**Không cần đọc**: §7 memory chi tiết, §8 DMA, phụ lục layout.

---

### SV2 — Paging 32-bit + Swap

**Việc cần làm**
1. `libmem.c`: ALLOC / FREE / READ / WRITE (dùng `vm_freerg_list`, tăng `sbrk` khi hết chỗ, FREE đưa region về free list).
2. `mm-vm.c`: `inc_vma_limit`, lấy vm_area; `mm.c`: `vmap_page_range`, chèn PTE, `__mm_swap_page`, chọn victim page; `mm-memphy.c`: tìm frame trống, `MEMPHY_read/write`, quản lý `free_fp_list`/`used_fp_list`.
3. Swap in/out giữa RAM và SWAP (1 RAM + tối đa 4 SWAP).
4. Phối hợp SV3 để syscall `memmap` (`SYSMEM_MAP_OP`, `INC_OP`, `SWP_OP`, `IO_READ`, `IO_WRITE`) hoạt động.
5. Page replacement (thuật toán do giảng viên quy định — **hỏi thầy**; `fifo_pgn` có sẵn).
6. Báo cáo: trạng thái data segment, Q4, Q6.

**Đọc kỹ**
- `spec.md §7.1, §7.3, §7.4, §7.6, §7.7` · PDF **p.13–15, p.16 (32-bit), p.17–20**
- `spec.md §5.1` (bảng `memmap`) · PDF p.9
- Hình **Figure 4** (vm_area/region, p.13), **Figure 6** (PTE, p.18), **Figure 7** (phân tầng module, p.20), **Figure 11** (toàn cảnh, p.25) — in ra/ghim lên.
- Code: `os-mm.h`, `mm.h`, `mm.c`, `mm-vm.c`, `mm-memphy.c`, `libmem.c`, `syscall.tbl`, `sys_*.c`.

**Tự tìm hiểu thêm**
- Page table, PTE bit layout (present/swapped/dirty), swap in/out, page fault.
- Thuật toán thay trang **FIFO, LRU, Clock**.
- Fragmentation (internal/external), `sbrk`/heap, free-list allocator (first-fit).
- Debug: in bảng trang/frame mỗi bước để kiểm chứng.

**Không cần đọc**: §6 scheduler, §8 DMA chi tiết, §7.5 64-bit (SV3 lo).

---

### SV3 — 64-bit paging + User/Kernel + Syscall

**Việc cần làm**
1. `mm64.c`: 5-level paging PGD→P4D→PUD→PMD→PT (9 bit/tầng, offset 12), dịch địa chỉ; cấp bảng theo kiểu **demand/sparse** để tiết kiệm bộ nhớ.
2. Canonical address + chọn layout user/kernel (khuyến khích kiểu macOS) — **hỏi chốt số liệu `KERNEL_BASE_ADDR`**.
3. Tách user/kernel: dùng `krnl_t`; syscall **truyền PID, không truyền PCB**; READ/WRITE chặn địa chỉ kernel; `copy_from_user`/`copy_to_user`; `kmalloc` (vật lý liên tục).
4. Syscall: `listsyscall` (0), phối hợp SV2 cho `memmap` (17); thêm syscall theo quy trình `sys_xxx.c` + `Makefile` + `syscall.tbl`.
5. Thống kê **số lần truy cập bộ nhớ** và **dung lượng bảng trang**; vẽ sơ đồ dịch địa chỉ.
6. Báo cáo: Q1, Q2, Q5.

**Đọc kỹ**
- `spec.md §7.2, §7.5, §7.8` · PDF **p.15–17** (canonical, 64-bit, ví dụ dịch địa chỉ), p.19 (advanced topics), p.21 (`MM64`)
- `spec.md §3.1` (READ/WRITE/KMALLOC/COPY_*) · PDF p.6–7
- `spec.md §5` (syscall) · PDF **p.8–11**
- Phụ lục layout: macOS (PDF p.31), Linux (p.29–30) — chỉ để chọn layout.
- Code: `mm64.h`, `mm64.c`, `bitopts.h`, `syscall.h`, `syscall.c`, `syscalltbl.sh`, `common.h` (`krnl_t`).

**Tự tìm hiểu thêm**
- x86-64 **4/5-level paging**, canonical address, CR3, TLB.
- Kernel/user split (Linux), `copy_from_user`/`copy_to_user` trong Linux kernel, cơ chế syscall (trap, bảng syscall).
- Sparse page table, hash page table.
- Lệnh bit: `(addr >> 12) & 0x1ff` — viết test dịch địa chỉ bằng ví dụ trong PDF p.17 (nhớ kiểm tra lại phép tính, xem `spec.md §13`).

**Không cần đọc**: §6 scheduler, §8 DMA chi tiết.

---

### SV4 — Co-processor (DMA) + Tích hợp + Báo cáo

**Việc cần làm**
1. Hiểu và build module DMA: `make dma`, chạy `./dma_testbench` và `./dma_testbench 0x1000 0x2000 1024`.
2. Tích hợp DMA vào OS: lệnh `dma_memcpy`, cơ chế **bất đồng bộ** (CPU không bị block, DMA báo hoàn thành bằng IRQ/interrupt mô phỏng).
3. **Bus addressing**: CPU truy cập qua MMU (phối hợp SV2/SV3), DMA truy cập bộ nhớ qua bus address (`dma_copy`/`get_ptr`).
4. Xử lý dữ liệu theo byte; kiểm tra `ch_error`, offset/size biên.
5. Quản lý **tích hợp**: Makefile, test chạy toàn hệ thống, theo dõi tiến độ.
6. **Tổng hợp báo cáo**, đóng gói `assignment_STUDENTID.zip`.

**Đọc kỹ**
- `spec.md §8` · PDF **p.21–24** (§2.3: async, DMA, pinout, I/O acceleration)
- Phụ lục software prototype DMA · PDF **p.33**
- `spec.md §3.1` (lệnh `dma_memcpy`) · PDF p.7
- `spec.md §11` + PDF p.26–27 (yêu cầu nộp, báo cáo).
- Code: `dma*` (RTL + testbench), `Makefile` (target `dma`, `os`), `cpu.c`, `syscall.*` (nơi gắn lệnh DMA).

**Tự tìm hiểu thêm**
- **DMA**, Programmed I/O vs DMA, interrupt-driven I/O, bus mastering.
- **Verilator**: cách build, testbench C++, đọc waveform (VCD/GTKWave).
- Endianness; giao thức handshake `dat_valid`/`dat_ready`.

**Không cần đọc kỹ**: §6 scheduler, §7 paging chi tiết (chỉ cần hiểu MMU là gì).

> Phần GPU/CUDA/OpenVINO chỉ dành cho "talent students", **không được cộng điểm** — bỏ qua.

## E. Giao diện giữa các module (mọi người cần biết)

| Bên cung cấp → bên dùng | Nội dung | Chốt vào |
|---|---|---|
| SV2 → SV3, SV4 | API paging cơ bản: map page, cấp frame, đọc/ghi RAM/SWAP | Ngày 3 (17/11) |
| SV3 → SV2, SV4 | Quy ước syscall `memmap`, truyền **PID** (không truyền PCB), phân vùng user/kernel | Ngày 4 (18/11) |
| SV4 → SV1, SV3 | Cách DMA báo hoàn thành (interrupt/flag), lock cho DMA channel | Ngày 8 (22/11) |
| SV1 → tất cả | Danh sách lock và **thứ tự lấy lock** (tránh deadlock) | Ngày 9 (23/11) |

Mọi thay đổi vào header dùng chung (`common.h`, `os-mm.h`, `syscall.h`, `os-cfg.h`) → **báo cả nhóm trước**.

## F. Lộ trình 15 ngày (14/11 → 29/11)

| Ngày | Mốc | Cả nhóm / mọi người | Ghi chú |
|---|---|---|---|
| **N0–N1** (14–15/11) | Khởi động | Đọc chung (mục B); build `make os`/`make dma`; chạy thử; mỗi người đọc phần của mình; gom câu hỏi mơ hồ gửi thầy; tạo repo/branch | Chốt công cụ, deadline giờ nộp |
| **N2–N4** (16–18/11) | Core v1 | **SV1** `enqueue/dequeue/get_proc` chạy được 1 CPU · **SV2** alloc/free/read/write trong RAM · **SV3** thiết kế user/kernel + PID passing, cấu trúc `mm64` · **SV4** chạy DMA testbench, hiểu giao diện | Chốt giao diện SV2↔SV3 (N3–N4) |
| **N5–N7** (19–21/11) | Core v2 | **SV1** nhiều CPU + khớp `output/` + Gantt · **SV2** swap in/out + victim page · **SV3** dịch địa chỉ 5 tầng + `copy_*_user` · **SV4** `dma_memcpy` async (bản software prototype trước) | |
| **N8 = 22/11** | **Milestone 1** | Scheduler hoàn chỉnh · paging 32-bit + swap chạy · DMA chạy riêng lẻ · họp đồng bộ 1 giờ | Ai trễ → báo ngay, chia lại việc |
| **N9–N11** (23–25/11) | Tích hợp | **SV3** MM64 demand allocation + thống kê · **SV4** DMA thật (verilator) gắn vào OS, bus addressing · **SV1** khoá toàn bộ shared resource, stress test nhiều CPU · **SV2** page replacement + hỗ trợ tích hợp | Chạy sanitizer |
| **N12 = 26/11** | **Milestone 2** | Toàn hệ thống chạy end-to-end với input mẫu & input riêng · **code freeze mềm** (chỉ sửa bug) | |
| **N12–N13** (26–27/11) | Báo cáo | Mỗi người viết phần mình + câu hỏi của mình; **SV4 ghép** · chụp output/Gantt/sơ đồ thật | Đừng bịa ví dụ, dùng output thực |
| **N13 = 27/11 tối** | **Code freeze** | Không merge tính năng mới | |
| **N14 = 28/11** | Review & diễn tập | Review chéo code/báo cáo; diễn tập demo (Scheduling 2đ · MMU/user-kernel 2đ · DMA 3đ); đóng gói `assignment_STUDENTID.zip` | Mỗi người trả lời được câu hỏi về code của mình |
| **N15 = 29/11** | **Nộp bài** | Nộp LMS sớm trong ngày; tuyệt đối không để phút cuối | Ngày đệm dự phòng |

**Thứ tự ưu tiên nếu bị trễ:** Scheduler → Paging 32-bit → User/Kernel → **DMA (3đ)** → 64-bit → đánh bóng đồng bộ. Mỗi ngày dành 15 phút họp ngắn: *hôm qua làm gì – hôm nay làm gì – vướng gì*.

## G. Tổng hợp câu hỏi trong đặc tả (phải trả lời trong báo cáo)

| # | Vị trí (PDF) | Câu hỏi | Người phụ trách |
|---|---|---|---|
| Q1 | §1.6.2, p.9 | Ưu/nhược của giao diện system call **hợp nhất** để thao tác nhiều thành phần (file, bộ nhớ, I/O)? Xét thiết kế OS, hiệu năng, độ phức tạp xử lý lỗi, portability vs efficiency | SV3 |
| Q2 | §1.6.3, p.11 | Khi một system call chạy quá lâu, OS phát hiện và xử lý như thế nào? | SV3 |
| Q3 | §2.1, p.13 | Xét tác động của các chính sách chi tiết của MLQ, lợi ích của từng chính sách là gì? | SV1 |
| Q4 | §2.2.2, p.17 | Động cơ chính khi kết hợp segmentation với paging? Cách lai khắc phục hạn chế của từng kỹ thuật riêng? | SV2 |
| Q5 | §2.2.3, p.18 | Lợi ích của việc mở rộng hierarchical paging lên N level? | SV3 |
| Q6 | §2.2.4, p.19 | Ưu/nhược điểm của paging và contiguous memory allocation? | SV2 |
| Q7 | §2.4, p.25 | Chuyện gì xảy ra nếu không xử lý đồng bộ? Minh hoạ bằng ví dụ từ output của bài | SV1 |

Gợi ý hướng trả lời từng câu nằm trong `spec.md §10` (khung ý), câu trả lời cuối do người phụ trách tự viết.

## H. Báo cáo & nộp bài

Bắt buộc có: **Gantt** (scheduling) · **trạng thái cấp phát data segment** + chứng minh **PID passing** · **sơ đồ dịch địa chỉ nhiều tầng** + **thống kê số lần truy cập bộ nhớ / dung lượng bảng trang** · diễn giải kết quả mô phỏng · trả lời đủ 7 câu hỏi.
Nộp: đặt báo cáo vào thư mục mã nguồn → nén thành `assignment_STUDENTID.zip` → nộp LMS.
Điểm: Demo 7đ (Scheduling 2 · MMU & user/kernel 2 · Co-processor 3) + Báo cáo 3đ.

## I. Git & quy ước

- Nhánh: `main` (luôn build được) ← `dev` ← `feature/<tên>-<phần>`; PR cần 1 người khác review + `make os` sạch.
- Code style **GNU C**; comment/commit bằng tiếng Anh. Không sửa `input/`, `output/` mẫu (test riêng để ở `input_team/`).
- File obsolete (`mem.h`, `loader.h`, `mem.c`, `paging.c`, `run_queue`, `pcb_t.page_table`) không xoá, không phụ thuộc.
- Quyết định/giả định đã duyệt ghi vào `docs/decisions.md`; câu hỏi chưa rõ ghi vào `docs/questions.md`.
- Dùng AI agent thì tuân thủ `AGENTS.md`; mọi code AI sinh ra phải được người nhận đọc hiểu và giải thích được.
