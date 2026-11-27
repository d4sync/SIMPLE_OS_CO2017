# SPEC — Simple Operating System (OSSIM)

> Tài liệu này là bản diễn giải lại đặc tả gốc `hk261_ossim_demacOprx.pdf` (HCMUT, CO2018 – Operating Systems, Assignment ngày 17/10/2026, v5.0).
> Mục đích: để mọi thành viên và mọi AI agent hiểu **dự án là gì, cần làm gì, hàm nào cần cài, cơ chế mong đợi ra sao**.
> Những chỗ đặc tả gốc **mơ hồ / mâu thuẫn** được gom ở mục [13. Điểm chưa rõ](#13-điểm-chưa-rõ--mâu-thuẫn-trong-đặc-tả-gốc). **Không được tự đoán các điểm này — phải hỏi lại chủ dự án.**

---

## 1. Tổng quan

Dự án **mô phỏng một hệ điều hành đơn giản** chạy trên phần cứng ảo, gồm 3 mảng chính:

1. **Scheduler / Dispatcher** — chọn process nào chạy trên CPU nào (thuật toán **Multi-Level Queue – MLQ**).
2. **Virtual Memory (paging)** — mỗi process có không gian địa chỉ ảo riêng, OS ánh xạ ảo → vật lý (RAM + SWAP), hỗ trợ **32-bit single-level** và **64-bit 5-level paging**, tách **user space / kernel space**.
3. **Co-processor (DMA)** — cơ chế bất đồng bộ giữa CPU và DMA; dùng DMA thật (verilator) cho phần demo nhóm.

Cộng thêm: **system call interface** và **đồng bộ hoá (synchronization)** vì OS chạy nhiều CPU.

Kiến trúc: `Software (Process 0..n) → OS (Scheduler+Dispatcher | Virtual Memory) → Hardware (CPU(s), Co-processor/DMA, Physical Memory) → System Bus`.

OS có **dual-mode**: user mode / kernel mode (mode bit). Chương trình user không được chạy lệnh đặc quyền hay truy cập địa chỉ kernel trực tiếp.

---

## 2. Cấu trúc mã nguồn (do giảng viên cung cấp)

**Header (`include/`)**

| File | Vai trò |
|---|---|
| `timer.h` | Timer cho toàn hệ thống |
| `cpu.h` | Hàm của CPU ảo |
| `queue.h` | Queue chứa PCB |
| `sched.h` | Hàm của scheduler |
| `mem.h`, `loader.h` | **(obsolete)** VM engine cũ / loader cũ |
| `common.h` | Struct & hàm dùng chung (`pcb_t`, `krnl_t`...) |
| `bitopts.h` | Thao tác bit |
| `os-mm.h`, `mm.h`, `mm64.h` | Cấu trúc & dữ liệu cơ bản cho paging |
| `syscall.h` | Header system call |
| `libxxx.h` | (tuỳ chọn) header thư viện chuẩn |
| `os-cfg.h` | (tuỳ chọn) các `#define` bật/tắt cấu hình |

**Source (`src/`)**

| File | Vai trò |
|---|---|
| `timer.c`, `cpu.c`, `os.c` (main) | Timer, CPU ảo, hàm `main` khởi động OS |
| `queue.c` | **Cần cài**: `enqueue()`, `dequeue()` |
| `sched.c` | **Cần cài**: `get_proc()` |
| `loader.c` | Loader nạp chương trình |
| `paging.c`, `mem.c` | **(obsolete)** chỉ để kiểm tra / thiết kế cũ |
| `mm.c`, `mm64.c`, `mm-vm.c`, `mm-memphy.c` | Paging (`mm64.c` = hỗ trợ 64-bit) |
| `libmem.c`, `libstd.c` | Thư viện chuẩn (alloc/free/read/write ở user-lib) |
| `syscall.c`, `syscall.tbl`, `syscalltbl.sh`, `sys_xxx.c` | System call |
| `dma*` | Engine DMA |

Khác: `Makefile`, `input/` (bộ input kiểm thử), `output/` (output mẫu).

---

## 3. Process và PCB

OS đa nhiệm; mỗi process = một **danh sách lệnh** chạy tuần tự từ đầu đến cuối (**không có lệnh jump**).

```c
// include/common.h
struct pcb_t {
    uint32_t pid;
    uint32_t priority;      // priority mặc định (legacy), thấp hơn = ưu tiên cao hơn, cố định
    char     path[100];
    uint32_t code_seg_t *code;  // text segment (không đặt trong RAM, để đơn giản)
    addr_t   regs[10];      // 10 thanh ghi, đánh số 0..9
    uint32_t pc;            // program counter
#ifdef MLQ_SCHED
    uint32_t prio;          // priority khi chạy (live priority), GHI ĐÈ priority mặc định
#endif
    struct page_table_t *page_table; // OBSOLETE, không dùng
    uint32_t bp;            // break pointer — quản lý heap segment
};
```

Khi `MM_PAGING` bật, `pcb_t` có thêm: `struct mm_struct *mm; struct memphy_struct *mram; struct memphy_struct **mswp; struct memphy_struct *active_mswp;`.

### 3.1 Tập lệnh của process

| Lệnh | Cú pháp | Ngữ nghĩa |
|---|---|---|
| `CALC` | `calc` | Tính toán bằng CPU, không tham số |
| `ALLOC` | `alloc [size] [reg]` | Cấp `size` byte từ RAM; địa chỉ byte đầu lưu vào thanh ghi `reg`. VD `alloc 124 7` |
| `KMALLOC` | `kmalloc [size] [reg]` | Cấp `size` byte ở **kernel space**, **liên tục về mặt vật lý** (OS phải tìm block vật lý liên tục); địa chỉ lưu vào `reg` |
| `FREE` | `free [reg]` | Giải phóng vùng nhớ mà `reg` đang giữ địa chỉ đầu |
| `READ` | `read [source] [offset] [destination]` | **Chỉ user space.** Đọc 1 byte tại `regs[source] + offset` → lưu vào `destination`. `offset` viết dạng **hex** (VD `read 1 20 2` với reg1=0x123 → đọc tại 0x123+0x14) |
| `WRITE` | `write [data] [destination] [offset]` | **Chỉ user space.** Ghi giá trị `data` vào địa chỉ `regs[destination] + offset` (offset hex, VD `write 10 1 20`) |
| `COPY_FROM_USER` | `copy_from_user [source] [destination] [offset] [size]` | Đọc trực tiếp từ user space ghi vào kernel space (từ `source` sang `destination`, bắt đầu tại `offset`, độ dài `size`) |
| `COPY_TO_USER` | `copy_to_user [source] [destination] [offset] [size]` | Đọc từ kernel space ghi ra user space (cùng tham số như trên) |
| `DMA_MEMCPY` | `dma_memcpy [source] [destination] [offset] [size]` | Copy nội dung vùng `source` → `destination` **cùng offset**, qua DMA |
| `syscall` | `syscall [number] [args...]` | Gọi system call (VD `syscall 440 1`) |

READ/WRITE bị cấm truy cập địa chỉ kernel (gây invalid memory operation). Muốn trao đổi dữ liệu user↔kernel phải dùng `copy_from_user` / `copy_to_user`.

### 3.2 Định dạng file chương trình (trong `input/proc/`)

```
[priority] [N = số lệnh]
instruction 0
...
instruction N-1
```

- `priority` là priority mặc định của process.
- **Dual priority**: nếu file cấu hình mô phỏng (mục 4) có priority cho process thì giá trị đó **ghi đè** priority mặc định trong file chương trình.

### 3.3 Memory region (biến)

Region = vùng nhớ cấp cho một "biến". Tên biến → index trong **symbol table** nằm ngoài phạm vi môn học (thuộc môn Compiler); dự án chỉ dùng **index** của region. Số biến mỗi process bị giới hạn bởi `PAGING_MAX_SYMTBL_SZ`.

---

## 4. Chạy mô phỏng

Build: `make all`. Chạy: `./os [configure_file]` (file cấu hình nằm trong `input/`).

### 4.1 Cấu hình cơ bản (khi `MM_FIXED_MEMSZ` bật — tương thích ngược)

```
[time slice] [N = số CPU] [M = số process]
[time 0]   [path 0]   [priority 0]
...
[time M-1] [path M-1] [priority M-1]
```

- `time slice`: thời gian (giây) mỗi process được chạy liên tục.
- `time i`: thời điểm process i được nạp; `path i`: file chương trình; `priority i`: **live priority** (ghi đè mặc định).

### 4.2 Cấu hình đầy đủ (khi **tắt/comment** `MM_FIXED_MEMSZ`)

```
[time slice] [N] [M]
[KERNEL_BASE_ADDR]
[RAM_SZ] [SWP_SZ_0] [SWP_SZ_1] [SWP_SZ_2] [SWP_SZ_3]
[time 0] [path 0] [priority 0]
...
```

- Phải có **1 RAM** và **≥ 1 SWAP** với kích thước **dương**; các SWAP còn lại có thể = 0 (nghĩa là vô hiệu). Tối đa **4 SWAP**.
- `KERNEL_BASE_ADDR`: hỗ trợ không gian kernel có địa chỉ cơ sở "động"; nếu không có thì dùng layout mặc định (đặc tả trỏ tới "Table ??" — bảng bị thiếu, xem mục 13).
- Dòng điều khiển bởi hằng `#define`; luôn kiểm tra kỹ cả file input lẫn `include/os-cfg.h` khi hành vi lạ.

### 4.3 Cấu hình qua `include/os-cfg.h`

```c
#define MLQ_SCHED 1
#define MAX_PRIO 140
#define MM_PAGING
#define MM_FIXED_MEMSZ
// (và MM64 cho cấu hình 64-bit)
```

Mục đích: cô lập từng tính năng bằng cờ biên dịch, giữ nhiều subsystem trong một codebase. Nếu đang làm scheduler thì **giữ mặc định**, không đổi nhiều.

### 4.4 Cấu trúc kernel

```c
struct krnl_t {
    struct queue_t *ready_queue;
    struct queue_t *running_list;
#ifdef MLQ_SCHED
    struct queue_t *mlq_ready_queue;
#endif
#ifdef MM_PAGING
    struct mm_struct *mm;
    struct memphy_struct *mram;
    struct memphy_struct **mswp;
    struct memphy_struct *active_mswp;
    uint32_t active_mswp_id;
#endif
};
```

---

## 5. System call

Cơ chế: process gọi `syscall(number, ...)` (thường qua wrapper trong `libstd`, wrapper chỉ nạp tham số vào thanh ghi rồi gọi) → tra **`syscall_tbl`** theo index → chạy handler `__sys_xxx` trong kernel → trả kết quả về process.

### 5.1 Danh sách system call

| Số | Tên | Mô tả |
|---|---|---|
| 0 | `listsyscall` | Liệt kê mọi system call (`SYSCALL 0`) |
| 17 | `memmap` | `SYSCALL 17 SYSMEM_OP REG_ARG2 REG_ARG3` — các thao tác bộ nhớ |

Các op của `memmap` và handler tương ứng:

| Op | Handler |
|---|---|
| `SYSMEM_MAP_OP` | `vmap_pgd_memset()` |
| `SYSMEM_INC_OP` | `inc_vma_limit()` |
| `SYSMEM_SWP_OP` | `__mm_swap_page()` |
| `SYSMEM_IO_READ` | `MEMPHY_read()` |
| `SYSMEM_IO_WRITE` | `MEMPHY_write()` |

### 5.2 Cách thêm một system call (ví dụ số 440, tên `xxx`)

1. Tạo `src/sys_xxxhandler.c`:
   ```c
   #include "common.h"
   #include "syscall.h"
   #include "stdio.h"
   int __sys_xxxhandler(struct pcb_t *caller, struct sc_regs *regs)
   {
       /* TODO: implement syscall job */
       printf("The first system call parameter %d\n", regs->a1);
       return 0;
   }
   ```
2. `Makefile`: `SYSCALL_OBJ += $(addprefix $(OBJ)/, sys_xxxhandler.o)`
3. `src/syscall.tbl`: thêm dòng `440  xxx  sys_xxxhandler`
4. `make all`, rồi chạy `./os <input_file>`.

**Kiểm thử mẫu**: chương trình `sc`:
```
20 1
syscall 440 1
```
File cấu hình `os_syscall`:
```
2 1 1
2048 16777216 0 0 0
9 sc 15
```
Chạy `./os os_syscall`; dòng cuối output phải có `The first system call parameter 1`, rồi `CPU 0: Processed 1 has finished`, `CPU 0 stopped`.

> Lưu ý: đặc tả gốc có lỗi chính tả trong code mẫu (`reg` vs `regs`, "paramter") — xem mục 13.

**Yêu cầu coding style**: tuân theo GNU C style: https://www.gnu.org/prep/standards/html_node/Writing-C.html

---

## 6. Scheduler (MLQ) — Phần 1

### 6.1 Mô hình

- Có `MAX_PRIO` mức ưu tiên (Linux: 140, `prio` = 0..139). **Mỗi priority = đúng 1 `ready_queue`** (đơn giản hoá).
- **Giá trị prio nhỏ = ưu tiên cao.**
- Luồng: loader tạo PCB cho mỗi chương trình, copy nội dung vào text segment (`code`), **đẩy PCB vào ready_queue có priority = `prio`** của process → chờ CPU.
- CPU chạy theo **round-robin** trong một queue: mỗi process chạy tối đa 1 time slice, hết slice thì CPU bị ép **đưa process về lại đúng ready_queue của nó** (`put_proc`), rồi lấy process khác (`get_proc`).
- `add_queue()` và `put_proc()` được đơn giản hoá = đặt process vào ready_queue theo priority tương ứng.
- Trọng tâm là chính sách MLQ nằm trong **`get_proc()`**.
- `run_queue` / `processed_queue` là **legacy/obsolete** — không cần dùng nhưng **giữ nguyên** để tránh lỗi khó truy.
- Không cài cơ chế feedback (di chuyển process giữa các queue) như Linux.

### 6.2 Chính sách MLQ

- Mỗi queue được cấp **slot** (số lượt dùng CPU cố định): `slot = MAX_PRIO - prio`.

  | prio | 0 | 1 | ... | MAX_PRIO-1 |
  |---|---|---|---|---|
  | slot | MAX_PRIO | MAX_PRIO-1 | ... | 1 |

- Khi một queue **dùng hết slot**, hệ thống chuyển tài nguyên sang process của **queue kế tiếp**; phần việc còn lại (slot chưa dùng) để cho lượt sau, kể cả khi chưa hoàn thành một vòng ready_queue.
- MLQ chỉ duyệt các queue theo bước cố định như trên.

### 6.3 Hàm cần cài

| Hàm | File | Yêu cầu |
|---|---|---|
| `enqueue()` | `queue.c` | Đặt PCB mới vào `queue_t` (priority queue đã định nghĩa trong `queue.h`) |
| `dequeue()` | `queue.c` | Lấy PCB "đến lượt" ra khỏi queue |
| `get_proc()` | `sched.c` | Lấy PCB của process đang chờ từ hệ thống ready_queue, chọn queue "đến lượt" theo chính sách MLQ ở 6.2 |

Kiểm tra: so với `output/` mẫu. Do loader và scheduler chạy **đồng thời**, có thể có **nhiều đáp án đúng**; kết quả chỉ cần **giải thích được và khớp lý thuyết**.

**Câu hỏi (Q3):** Xét tác động của các chính sách chi tiết của MLQ, lợi ích của từng chính sách là gì?

---

## 7. Quản lý bộ nhớ — Phần 2

### 7.1 Bố cục bộ nhớ process

- Không gian ảo của mỗi process = nhiều **`vm_area`** liền kề (thực tế mỗi area có thể là code/stack/heap).
- Mỗi area trải trên `[vm_start, vm_end]` nhưng phần dùng được bị giới hạn bởi con trỏ **`sbrk`**.
- Giữa `vm_start` và `sbrk` có nhiều **region** (`vm_rg_struct`) và các lỗ trống theo dõi bằng **`vm_freerg_list`**.
- Chỉ cấp phát vật lý thực sự trong vùng sử dụng được.

```c
// include/os-mm.h
struct vm_rg_struct {
    unsigned long rg_start;
    unsigned long rg_end;
    unsigned long mode_bit;   // user mode = 1, kernel mode = 0
    struct vm_rg_struct *rg_next;
};

struct vm_area_struct {
    unsigned long vm_id;
    unsigned long vm_start;
    unsigned long vm_end;
    unsigned long sbrk;
    // derived: vm_limit = vm_end - vm_start
    struct mm_struct *vm_mm;
    struct vm_rg_struct *vm_freerg_list;
    struct vm_area_struct *vm_next;
};

struct mm_struct {
    uint32_t *pgd;                         // (MM64: pgd, p4d, pud, pmd, pt kiểu uint64_t*)
    struct vm_area_struct *mmap;
    struct vm_rg_struct symrgtbl[PAGING_MAX_SYMTBL_SZ];  // symbol table đơn giản
    struct pgn_t *fifo_pgn;                // theo dõi page (FIFO, tham khảo)
    struct kcache_pool_struct *kcpooltbl;  // kmem cache pool
};
```

- `symrgtbl[PAGING_MAX_SYMTBL_SZ]` lưu start/end của từng region theo index; `rg_next` dự phòng.
- Chỉ tập trung vào **vm_area đầu tiên (vmaid = 0)**; phiên bản sau mới xét nhiều segment và chồng lấn.

### 7.2 Địa chỉ canonical & layout

- Trên 64-bit, phần cứng chỉ dùng 48 hoặc 57 bit; các bit cao không dùng phải **cố định 0 hoặc 1** → **canonical address**.
- Phụ lục đặc tả có layout Linux (lab CO2018 dùng), NT kernel, macOS. **Học kỳ này khuyến khích dùng layout kiểu macOS** (phù hợp quản lý vùng nhỏ/vừa/lớn tách biệt, phần cứng tăng tốc thao tác bộ nhớ).
- Kernel memory: **không trọng tâm**, dùng cấp phát liên tục đơn giản. Nhưng **bắt buộc có quyền truy cập đặc quyền**: user chỉ R/W địa chỉ user; địa chỉ kernel bị cấm → phải dùng `copy_from_user`/`copy_to_user`.

### 7.3 Địa chỉ 32-bit (single-level paging)

Địa chỉ CPU = **page number (p)** + **page offset (d)**. Vật lý chia thành **frame** cỡ 256B hoặc 512B. Các cấu hình (Table 1):

| CPU bus | Page size | Page bit | # entry | Entry size | Page table | Offset bit | PGT mem | MEMPHY | Frame bit |
|---|---|---|---|---|---|---|---|---|---|
| 20 | 256B | 12 | ~4000 | 4B | 16KB | 8 | 2MB | 1MB | 12 |
| **22** | **256B** | **14** | **~16000** | 4B | **64KB** | **8** | 8MB | 1MB | 12 |
| 22 | 512B | 13 | ~8000 | 4B | 32KB | 9 | 4MB | 1MB | 11 |
| 22 | 512B | 13 | ~8000 | 4B | 32KB | 9 | 4MB | 128KB | 8 |
| 16 | 512B | 8 | 256 | 4B | 1KB | 9 | 128K | 128KB | 4 |

Cấu hình tô sáng/đề xuất: **CPU bus 22 bit, page 256B** (page number 14 bit [21:8], offset 8 bit [7:0]). Mỗi process có **1 page table riêng** phủ toàn bộ không gian địa chỉ CPU (VD 16000 entry × 4B = 64KB/process).

### 7.4 Page table entry (32-bit; áp dụng cho cả 32/64 theo đặc tả)

| Bit | Ý nghĩa |
|---|---|
| 31 | Present |
| 30 | Swapped |
| 29 | Reserved |
| 28 | Dirty |
| 15–27 | User-defined numbering (nếu present) |
| 13–14 | Zero (nếu present) |
| 0–12 | **FPN** — page frame number (nếu present) |
| 0–4 | **Swap type** (nếu swapped) |
| 5–25 | **Swap offset** (nếu swapped) |

Page có thể ở: RAM (present), SWAP (swapped), hoặc null/N/A.

### 7.5 Địa chỉ 64-bit — 5-level paging (`MM64`)

| Bit | 63–57 | 56–48 | 47–39 | 38–30 | 29–21 | 20–12 | 11–0 |
|---|---|---|---|---|---|---|---|
| Level | — | L5 | L4 | L3 | L2 | L1 | OFFSET |
| Directory | — | PGD | P4D | PUD | PMD | PT | OFFSET |
| Phủ | — | 128PiB | 256TB | 512GB | 1GB | 2MB | 4KB |

Mỗi level index 9 bit: `idx = (addr >> shift) & 0x1ff` với shift = 12 (PT), 21 (PMD), 30 (PUD), 39 (P4D), 48 (PGD).
Ví dụ đặc tả: `0x39ffe1d9c9000` → PT=0x1c9, PMD=0x0ec, PUD=0x1f8, P4D=0x13f, PGD=0x03.

Page table 64-bit rất lớn → chiến lược giảm: **sparse allocation (hashing)** và **demand/dynamic allocation** (chỉ tạo bảng cho vùng đang dùng). Sinh viên được khuyến khích tự quyết thiết kế bằng cách cân nhắc **thời gian duyệt bảng, số lần truy cập bộ nhớ, dung lượng lưu trữ**. Mã hỗ trợ VM nằm ở `mm-vm.c`, `mm.c`; 64-bit ở `mm64.c`.

Nâng cao: có thể dùng mô hình truy cập thống nhất, hoặc tách thành 2 page table (user / kernel). Bắt buộc về lý thuyết: **kernel space phải giống hệt nhau ở mọi process**.

### 7.6 Bộ nhớ vật lý

- Hai loại thiết bị: **RAM** (truy cập trực tiếp từ bus CPU) và **SWAP** (thứ cấp, mọi thao tác phải đưa dữ liệu vào RAM trước). Cùng cài đặt bằng `mm-memphy.c` với cấu hình khác nhau (truy cập ngẫu nhiên / tuần tự, dung lượng).
- Hỗ trợ **1 RAM + tối đa 4 SWAP**.

```c
struct framephy_struct { int fpn; struct framephy_struct *fp_next; };

struct memphy_struct {
    BYTE *storage;
    int maxsz;
    int rdmflg;   // 1: truy cập ngẫu nhiên / 0: tuần tự
    int cursor;   // cho thiết bị tuần tự
    struct framephy_struct *free_fp_list;   // frame trống
    struct framephy_struct *used_fp_list;   // frame đã dùng
};
```

### 7.7 Các thao tác bộ nhớ trong hệ paging

- **ALLOC** (`libmem`): tìm region trống trong `vm_freerg_list`; nếu có → dùng. Nếu không → **tăng `sbrk`** (qua syscall `memmap`/`SYSMEM_INC_OP` → `inc_vma_limit`), rồi xin frame vật lý và **map vào page table** (`vmap_page_range`, insert PTE), được region tại giới hạn mới.
- **FREE**: không trả frame vật lý (tránh lỗ nhớ) — đưa region vào **`vm_freerg_list`** để dùng lại. Logic nằm trong `libmem`.
- **READ/WRITE**: page phải ở RAM. Nếu page đang ở SWAP → **swap in**; nếu RAM hết chỗ → chọn **victim page** và **swap out** sang SWAP (qua `memmap`/`SYSMEM_SWP_OP` → `__mm_swap_page`; dữ liệu qua `SYSMEM_IO_READ/WRITE` → `MEMPHY_read/write`).
- **Swapping**: swap in = copy frame từ SWAP → RAM; swap out = RAM → SWAP.

**Phân tầng module (Figure 7):**

| Module | Việc chính |
|---|---|
| `LIBMEM` | alloc/free/read/write ở mức thư viện; gọi `SYSCALL MEMINC`, `MEMSWP`, `MEMIO` |
| `MM-VM` | tăng giới hạn VM area, lấy VM area, `SWAP FRAME [MRAM<->MSWP]`, lấy frame number |
| `MM` | `vmap_page_range`, insert page-table entry, tìm victim page, swap copy RAM↔SWP |
| `MEMPHY` | tìm frame trống, IO truy cập memphy |

### 7.8 Tách user/kernel space (yêu cầu bắt buộc)

- Không gian user và kernel **tách biệt**. **Không được truy cập trực tiếp từ user space qua PCB của process; chỉ được dùng `struct krnl_t`.**
- Giao tiếp kernel ↔ user qua system call phải **truyền PID**, **không truyền trực tiếp con trỏ PCB** (báo cáo phải chứng minh điều này).

### 7.9 Câu hỏi phần Memory

- **Q4**: Mục đích chính khi kết hợp segmentation với paging? Cách tiếp cận lai khắc phục hạn chế của từng kỹ thuật riêng lẻ ra sao?
- **Q5**: Lợi ích của việc mở rộng hierarchical paging lên N level?
- **Q6**: Ưu/nhược điểm của paging so với contiguous memory allocation?

---

## 8. Co-processor — Phần 3

### 8.1 Bối cảnh

- **Asynchronous I/O**: CPU phát yêu cầu I/O rồi chạy tiếp; khi I/O xong, controller phát **interrupt**, CPU tạm dừng, chạy interrupt handler, rồi quay lại tác vụ cũ (Figure 8: 1 driver khởi tạo I/O → 2 → 3 I/O controller làm → 4 interrupt → 5 handler xử lý → 6 CPU resume → 7).
- CPU và co-processor trao đổi dữ liệu qua CPU-controlled transfer hoặc trực tiếp với bộ nhớ qua **bus address**; co-processor báo CPU bằng interrupt.

### 8.2 DMA

- Không có DMA: CPU phải đọc từng byte từ thiết bị vào thanh ghi rồi ghi lại RAM (Programmed I/O).
- Có DMA: CPU báo cho DMA **nguồn, đích, kích thước**, rồi chạy việc khác; DMA chiếm system bus chuyển dữ liệu, **chỉ báo CPU khi xong cả transfer**. Cũng dùng được cho memory-to-memory.
- Cấu hình: `ADDR_WIDTH = 64` bit, `DATA_WIDTH = 32` bit. Do vấn đề endianness/ABI, **khuyến khích xử lý dữ liệu theo từng byte**.

**Pinout của `dma_top`:**

| Tín hiệu | Hướng | Rộng | Mô tả |
|---|---|---|---|
| CLK | In | 1 | Clock |
| RST_N | In | 1 | Reset |
| IRQ | Out | 1 | Yêu cầu interrupt tới CPU |
| STATE | Out | 2 | Trạng thái hiện tại của DMA |
| ch_en | In | 1 | Bật kênh DMA |
| ch_pause | In | 1 | Tạm dừng kênh |
| ch_done | Out | 1 | Kênh hoàn thành transfer |
| ch_error | Out | 1 | Có lỗi khi transfer |
| ch_xfer_count | Out | 32 | Tổng số transfer đã xong |
| dat_valid | In | 1 | Dữ liệu vào hợp lệ |
| dat_0 | In/Out | ADDR_WIDTH | Dòng địa chỉ 0 |
| dat_1 | In/Out | ADDR_WIDTH | Dòng địa chỉ 1 |
| dat_2 | In/Out | DATA_WIDTH | Dòng dữ liệu 2 |
| dat_3 | In/Out | DATA_WIDTH | Dòng dữ liệu 3 |
| dat_ready | Out | 1 | DMA sẵn sàng |

### 8.3 Phần cứng yêu cầu khi demo

- **Mọi sinh viên**: engine DMA được cung cấp. Có **software prototype** `dma_copy()` (phụ lục) để phát triển/test sơ bộ:
  ```c
  int dma_copy(int bank, uint64_t src_offset, uint64_t dst_offset, uint64_t length) {
      // src = get_ptr(src_offset); dst = get_ptr(dst_offset);  // "BUS addressing" qua get_ptr()
      // if (src && dst) memcpy(dst, src, length);
      // return (src && dst) ? 0 : -1;
  }
  ```
- **Nhưng mọi project nhóm và presentation phải dùng DMA chip thật**, chạy trên nền phần cứng **verilator**.
- **Talent students (tuỳ chọn)**: tự thiết kế co-processor GPU hỗ trợ CUDA và OpenVINO (tính ma trận/vector). **Không được cộng điểm.**
- GPU: song song hoá cực lớn, tập trung render đồ hoạ và xử lý vector/ma trận.

### 8.4 Các công nghệ tăng tốc I/O (chỉ tham khảo khái niệm)

Memory operation offloading; Traffic/network optimization; Extended signaled interrupt; Low-latency interrupt; Streaming Accelerator; Pass-through I/O.

### 8.5 Yêu cầu cài đặt

Cài **cơ chế tương tác bất đồng bộ giữa CPU và co-processor**; lệnh `DMA_MEMCPY` đi qua DMA; có 2 phía truy cập bộ nhớ cần chứng minh: **(a) CPU truy cập qua MMU**, **(b) co-processor truy cập bộ nhớ qua bus address**.

---

## 9. Đồng bộ hoá — Phần 4 ("Put It All Together")

- Kết hợp scheduler + memory management thành OS hoàn chỉnh.
- OS chạy nhiều CPU ⇒ tài nguyên dùng chung có thể bị truy cập đồng thời ⇒ **phải xác định tài nguyên dùng chung và bảo vệ bằng cơ chế khoá (lock)**.
- Ví dụ tài nguyên cần xem xét: các ready_queue / mlq_ready_queue, running_list, `mm_struct` / page table, free frame list / used frame list của `memphy_struct`, `vm_freerg_list`, SWAP device, bộ sinh PID, DMA channel…
- Kiểm tra: `make all` rồi so với `output/` mẫu (nhiều kết quả đúng có thể có; chỉ cần giải thích được và khớp lý thuyết).
- **Câu hỏi (Q7)**: Chuyện gì xảy ra nếu không xử lý đồng bộ trong Simple OS? Minh hoạ bằng ví dụ (từ output của bài) nếu có trong các thao tác bộ nhớ kernel.

---

## 10. Các câu hỏi cần trả lời trong báo cáo (tổng hợp)

| # | Vị trí | Câu hỏi |
|---|---|---|
| Q1 | §1.6.2 System call | Ưu/nhược điểm của giao diện system call hợp nhất để thao tác nhiều thành phần (read/write/free cho file, bộ nhớ, thiết bị I/O)? Xét ảnh hưởng tới thiết kế OS, đánh đổi hiệu năng, độ phức tạp xử lý lỗi, cân bằng portability vs efficiency |
| Q2 | §1.6.3 | Khi một system call chạy quá lâu, OS phát hiện và xử lý thế nào? |
| Q3 | §2.1 Scheduler | Xét tác động của các chính sách chi tiết của MLQ, lợi ích của từng chính sách? |
| Q4 | §2.2.2 | Động cơ chính của việc kết hợp segmentation + paging? Cách lai khắc phục hạn chế của từng kỹ thuật riêng? |
| Q5 | §2.2.3 | Lợi ích của việc mở rộng hierarchical paging lên N level? |
| Q6 | §2.2.4 | Ưu/nhược điểm của paging và contiguous memory allocation? |
| Q7 | §2.4 | Điều gì xảy ra nếu không xử lý đồng bộ? Minh hoạ bằng ví dụ từ output |

---

## 11. Yêu cầu nộp bài & chấm điểm

### 11.1 Yêu cầu

- **Scheduler**: MLQ như §6.
- **Memory management** tách user/kernel (§7.8).
- **Multi-level paging**: 64-bit theo lý thuyết nhiều tầng (§7.5).
- **Page replacement (tuỳ chọn)**: do giảng viên quy định (vì 64-bit phức tạp).
- Trong report & output phải có **thống kê số lần truy cập bộ nhớ** và **dung lượng lưu trữ của multilevel paging**, kèm giải thích lợi ích của thiết kế.
- **Co-processor**: cơ chế bất đồng bộ CPU ↔ co-processor, DMA thật (verilator).
- **Questionnaire**: trả lời hết câu hỏi trong đặc tả.
- Coding style: GNU C.

### 11.2 Báo cáo

- Scheduling: **vẽ Gantt diagram** mô tả cách CPU chạy các process.
- Memory management: **trạng thái cấp phát bộ nhớ trong data segment**; chứng minh giao tiếp kernel↔user qua system call dùng **PID passing**.
- Multi-level paging: trình bày **sơ đồ dịch địa chỉ** nhiều tầng.
- Tổng thể: tự diễn giải kết quả mô phỏng.
- Cuối cùng: đưa báo cáo vào thư mục mã nguồn, nén cả thư mục thành **`assignment_STUDENTID.zip`**, nộp lên **LMS**.

### 11.3 Chấm điểm (nhóm 4–5 SV)

| Hạng mục | Điểm |
|---|---|
| Demo — Scheduling | 2 |
| Demo — MMU & user/kernel space (phần địa chỉ có chồng lấn với mục sau nhưng tính vào điểm mục sau) | 2 |
| Demo — Co-processor | 3 (1đ cho asynchronous; 2đ cho bus addressing: 1đ CPU truy cập qua MMU + 1đ co-processor truy cập bộ nhớ) |
| Báo cáo | 3 |
| **Tổng** | **10** (demo 7 + report 3) |

### 11.4 Đạo đức

Mã nguồn thuộc bản quyền giảng viên; chỉ được dùng/sửa để học trong môn CO2018 tại HCMUT. Không sao chép bài, không phát tán.

---

## 12. Lịch sử phiên bản đặc tả

1.0 (01/2019) CPU, scheduling, memory · 1.1 (09/2022) MLQ · 2.0 (03/2023) MM paging · 2.1 (10/2023) page replacement · 2.2 (03/2024) TLB · 2.3 (10/2024) heap segment · 3.0 (03/2025) syscall · 4.0 (10/2025) 64-bit · 4.1 (03/2026) canonical address · 5.0 (10/2026) co-processor.

---

## 13. Điểm chưa rõ / mâu thuẫn trong đặc tả gốc

> **Agent: các mục dưới đây KHÔNG được tự quyết. Hỏi chủ dự án (hoặc giảng viên) trước khi triển khai phần liên quan.**

1. **Slot MLQ**: công thức `slot = MAX_PRIO - prio` đã rõ, nhưng thứ tự duyệt queue (từ prio 0 lên? vòng tròn?), cách reset slot sau mỗi vòng, và cách xử lý khi queue rỗng chưa mô tả chi tiết.
2. **`pcb_t.code`** ghi `uint32_t code_seg_t *code;` — khai báo không hợp lệ về cú pháp C; cần đối chiếu với `common.h` thật.
3. **Code mẫu syscall** khai báo tham số `reg` nhưng dùng `regs->a1`; output mẫu viết "paramter". Cần theo mã nguồn thật.
4. **File cấu hình `os_syscall` mẫu** có dòng `2048 16777216 0 0 0` nhưng **không có dòng `KERNEL_BASE_ADDR`** trong khi §4.2 yêu cầu. Cần xác định cấu hình đúng.
5. **"Table ??"** (layout kernel mặc định khi không có `KERNEL_BASE_ADDR`) bị thiếu tham chiếu.
6. **Table 1**: hai dòng 22-bit/512B có cột MEMPHY và frame bit khác nhau (1MB/11 bit vs 128KB/8 bit); dòng 20-bit, 16-bit có số liệu có vẻ không nhất quán (VD 16-bit/512B nhưng page bit 8 và offset 9). Cần xác nhận cấu hình dùng.
7. **PTE**: hình vẽ & danh sách bit mô tả cho 32-bit; đặc tả nói áp dụng "32-bit & 64-bit" nhưng 64-bit PTE là `uint64_t` — chưa nêu layout bit 64-bit.
8. **Ví dụ 5-level paging**: giá trị PGD index ghi `0x03` — cần kiểm tra lại phép tính; và địa chỉ minh hoạ có bit 56–63 không phải canonical thuần.
9. **Layout kernel/user cho 64-bit**: khuyến khích macOS-style nhưng không có số liệu cụ thể (`KERNEL_BASE_ADDR` bao nhiêu, region nhỏ/vừa/lớn chia sao).
10. **`kmalloc`**: yêu cầu "physically contiguous" nhưng mục kernel memory lại nói "không trọng tâm, cấp phát liên tục đơn giản" — mức độ yêu cầu chưa rõ; `kcpooltbl` (kmem cache pool) có nhiệm vụ gì chưa mô tả.
11. **Page replacement**: do giảng viên quy định — chưa rõ chọn thuật toán nào (FIFO có `fifo_pgn` sẵn; LRU/Clock?).
12. **TLB**: lịch sử phiên bản có "CPU TLB" (v2.2) nhưng spec hiện tại không nói; cần hỏi có yêu cầu không.
13. **DMA thật / verilator**: không nêu cách build, giao diện tích hợp (file Verilog, wrapper C++, cách gọi từ `dma_memcpy`), "bank" của `dma_copy` nghĩa là gì, `get_ptr()` ở đâu.
14. **Cơ chế bất đồng bộ CPU↔DMA**: chưa nói rõ interrupt mô phỏng thế nào (thread riêng? callback? polling `IRQ`?).
15. **`copy_from_user`/`copy_to_user`/`dma_memcpy`**: ý nghĩa `source`/`destination` là register index hay region index, và `offset` có áp dụng cho cả hai đầu hay không — chưa rõ.
16. **Sample output** trong `output/` chỉ là tham khảo; tiêu chí "giải thích được và khớp lý thuyết" không có ngưỡng định lượng.
17. **Thống kê** "số lần truy cập bộ nhớ" và "dung lượng multilevel paging": định dạng báo cáo/ở đâu in ra chưa quy định.
18. **Deadline nộp, nhóm, thời gian demo**: đặc tả không nêu ngày cụ thể.
