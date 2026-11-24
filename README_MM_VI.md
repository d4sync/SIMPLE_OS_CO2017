# demacOprx – Paging, System Call và USR

Bản hiện thực phục vụ môn CO2018, HK261. Dựa trên ZIP demacOprx và tài liệu `hk261_ossim_demacOprx.pdf` (17/10/2026). Giữ nguyên LICENSE, các testcase và output gốc. Chỉ hỗ trợ cấu hình đang được bật trong ZIP: `MM64`, `MM_PAGING`, `MLQ_SCHED`. Không tuyên bố hoàn thành toàn bộ BTL (DMA và Scheduler là các phần của nhóm).

## 1. Chạy chương trình

Mở WSL/Ubuntu, giải nén và `cd` vào thư mục `hk261_ossim_demacOprx` (có Makefile).

```bash
make os-mm
./os-mm os_mm_usr_demo
make test-mm
```

`os-mm` là bản OS chạy các module C thực tế, không link RTL. Không có stub giả lập thành công DMA: `DMA_MEMCPY` trả `-ENOSYS` (-38). `test-mm` chạy assertion của các hàm bộ nhớ, syscall và CPU, không yêu cầu Verilator.

```bash
# Kiểm thử memory safety của các module chính
make test-mm MM_TEST_FLAGS='-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer'

# Full build trên VM môn học có phiên bản Verilator tương thích model Vdma_top gốc
make clean
make os
./os os_mm_usr_demo
```

Full build đã được thử trong môi trường thực hiện nhưng dừng vì thiếu `verilated.h`. Không thể xác nhận full RTL build hoặc DMA trong môi trường này. Không cần thay đổi file generated Vdma_top; hãy dùng VM môn học. Xem `tests/results/` để đọc log thực tế. `make os-mm` có thể hiển thị warning unused parameter từ `src/mem.c` legacy, không phải lỗi build.

## 2. Cấu trúc và file thay đổi

| File | Vai trò / thay đổi |
|---|---|
| `include/common.h` | Thêm registry link của PCB, danh sách process và kernel base của krnl_t; giữ 10 register. |
| `include/os-mm.h` | Thêm mode_bit, counter page table/walk/access; bảo đảm MM64 được thấy dù include độc lập. |
| `include/mm64.h`, `include/mm.h` | Hằng số, offset 12 bit, prototype, OVERLAP/INCLUDE half-open, include guard. |
| `include/syscall.h`, `include/libmem.h` | Interface guard, SC_INTERNAL, operation nội bộ và size/offset 64-bit. |
| `src/mm-memphy.c` | Free/used frame, zero-fill, bounds check, contiguous allocation, ownership, cleanup. |
| `src/mm64.c` | 5-level page walk, canonical, PTE, transactional mapping, unmap/prune, counters, dump, destroy. |
| `src/mm-vm.c` | VMA lookup, kiểm tra chồng lấn, sorted/coalesced free list, INC, swap copy primitive. |
| `src/libmem.c` | Wrapper PID, ALLOC/FREE/READ/WRITE, KMALLOC, copy, permission/bounds, cleanup. |
| `src/syscall.c`, `src/sys_mem.c`, `src/libstd.c`, `src/sys_listsyscall.c` | Registry, dispatcher, handler và trả lỗi. Syscall table gốc giữ nguyên. |
| `src/cpu.c`, `src/loader.c`, `src/os.c` | Kiểm tra register, lưu READ result, parse instruction/config, khởi tạo/thu hồi MM và PID, log lỗi. |
| `src/queue.c`, `src/sched.c`, `include/sched.h` | Tích hợp Scheduler tối thiểu: source gốc còn TODO, nên cần FIFO queue và MLQ quota để chạy OS; running list được cập nhật. |
| `src/timer.c` | Sửa synchronization của predicate done, atomic time/stop. |
| `Makefile` | Target os-mm, test-mm, dependency tạo syscalltbl.lst, clean. |
| `input/os_mm_usr_demo`, `input/proc/mm_usr_demo` | Test mới cho 2 CPU, kernel base động, copy và tái sử dụng. |
| `tests/test_mm.c`, `tests/results/`, `docs/` | Assertion tests, logs, hướng dẫn và báo cáo LaTeX. |

`src/mm.c`, `src/mem.c`, `src/paging.c` là code legacy 32-bit/không paging trong ZIP, không dùng cho đường MM64 hiện tại. Giữ nguyên các file này. Các con trỏ `p4d/pud/pmd/pt` cũ trong mm_struct giữ lại để tương thích source, nhưng không dùng như mảng phẳng; mọi nhánh đi qua `pgd`.

## 3. Kiến trúc tổng thể

```text
CPU run(proc)
  -> liballoc / libread / libwrite / libkmem_* / libsyscall
     -> _syscall(proc->krnl, proc->pid, 17, &regs)
        -> mm_lookup(kernel, pid)
        -> __sys_memmap(kernel, pid, &regs)
           -> __alloc / __read / __write / __kmalloc / mm_copy_region
              -> mm-vm (VMA / free region / INC)
              -> mm64 (walk / map / translate)
              -> mm-memphy (frame / physical RAM)
```

`krnl_t` sở hữu RAM, SWAP, registry và một cây bảng trang kernel. Mỗi PCB có một `mm_struct` riêng cho user, chứa cây bảng trang riêng và danh sách các region mà process sở hữu. Kernel region cũng có descriptor trong symbol table của PCB sở hữu, nhưng PTE của nó ở cây kernel chung. Việc trùng reg ID giữa hai process không làm chúng thấy dữ liệu của nhau.

Boundary: wrapper chỉ truyền `krnl_t*`, PID, syscall number và `sc_regs`, không truyền PCB qua dispatcher. Handler tra registry trong kernel và mới được lấy PCB thật. Các helper C dùng PCB sau lookup đều là code nội bộ mô phỏng kernel. Đây là mô phỏng phân quyền ở cấp chương trình, không phải hardware ring bảo mật thực sự.

## 4. Paging – đọc `mm64.c` trước

### 4.1. Canonical address và chỉ số 5 cấp

Page size = 4096 = 2^12 bytes. Virtual address có 57 bit có nghĩa. Bits 63–57 phải bằng bit 56.

| Thành phần | Bits | Cách lấy |
|---|---|---|
| PGD | 56–48 | `(va >> 48) & 0x1ff` |
| P4D | 47–39 | `(va >> 39) & 0x1ff` |
| PUD | 38–30 | `(va >> 30) & 0x1ff` |
| PMD | 29–21 | `(va >> 21) & 0x1ff` |
| PT | 20–12 | `(va >> 12) & 0x1ff` |
| offset | 11–0 | `va & 0xfff` |

Ví dụ tài liệu p.17: VA `0x39ffe1d9c9000` cho PGD=0x003, P4D=0x13f, PUD=0x1f8, PMD=0x0ec, PT=0x1c9. Test 01 kiểm tra đúng cả năm chỉ số. Nếu truy cập VA `...9c9abc` thì offset = 0xabc. Nếu PTE có frame 7 thì PA = 7*4096 + 0xabc = 0x7abc. FPN=0 là hợp lệ (present bit mới cho biết mapping tồn tại).

### 4.2. Cây page table cấp phát động

`init_mm` cấp phát PGD (512 slot 8 byte) và VMA0 rỗng. Hàm `walk` lần lượt lấy 5 index, dùng con trỏ từ entry cấp trên để đi xuống cấp dưới. Nếu `create=1` và entry chưa tồn tại, `new_table` cấp phát 512 slot zero. Nếu `create=0` thì không tự tạo bảng khi đọc, trả NULL nếu thiếu nhánh. PT cuối chứa giá trị PTE 32-bit trong slot 64-bit, không phải con trỏ.

```text
PGD[3] -> P4D[0x13f] -> PUD[0x1f8] -> PMD[0x0ec] -> PT[0x1c9] = PTE
                                                                            |
                                                               FPN + offset -> RAM
```

`pte_set_fpn` tạo PTE present; `pte_get_entry` trả giá trị chứ không trả địa chỉ của biến con trỏ cục bộ (lỗi của skeleton); `pte_set_entry` cập nhật leaf. `mm_translate` lấy FPN, ghép offset, đặt dirty khi ghi. `prune` giải phóng bảng con rỗng sau unmap. `destroy_table` duyệt đệ quy chỉ qua nhánh đã tồn tại, thu hồi frame rồi giải phóng các bảng. Không lặp qua 2^45 trang.

### 4.3. PTE và giới hạn RAM

PTE theo Figure 6 p.18: bit 31 present, bit 30 swapped, 29 reserve, 28 dirty, 27–15 user-defined numbering, 14–13 zero, 12–0 FPN. Khi swapped, bits 25–5 swap offset, 4–0 swap type. USR Number được để 0 (không phải PID hoặc bit privilege). Quyền region là `mode_bit` riêng. FPN 13 bit chỉ biểu diễn được 8192 frame, tức 32 MiB RAM có thể ánh xạ với page 4 KiB. Một số config gốc khai báo RAM 256 MiB, chương trình không cắt bit cao làm alias; chỉ dùng tối đa 8192 frame, log usable_frames. Đây là giới hạn cố ý để tuân thủ PTE 32-bit của đề. Nếu giảng viên yêu cầu dùng toàn bộ 256 MiB, cần thống nhất cách mở rộng FPN/PTE với nhóm trước khi thay đổi format.

### 4.4. Số lần truy cập và kích thước

`mm_stats`: số bảng mỗi cấp, `bytes`, `peak_bytes`, `walks`, `pt_access`, `data_access`, `total`. `pt_access` đếm từng entry được đọc trong page walk dịch địa chỉ (có thể ít hơn 5 nếu thiếu nhánh); `data_access` đếm byte read/write thành công và zero-fill vùng user/operation MAP. `total` = hai số trên. Không tính host malloc/calloc, thao tác quản lý bảng trang, zero-fill physical frame khi cấp phát, truy cập debug và swap primitive vào các counter này; đây là thống kê mô phỏng translation+data, không phải hardware performance counter. Test 02 kiểm tra một byte READ hợp lệ tăng pt_access đúng 5, data_access đúng 1. COPY mỗi byte gồm một read và một write: lý tưởng 12 access mô phỏng cho hai cây. Không triển khai TLB.

Một nhánh đầy đủ: 5 bảng x 512 x 8 = 20,480 B = 20 KiB. PGD trống = 4096 B. Một flat table 45-bit page number x 4 B/PTE = 2^45*4 B = 128 TiB, không khả thi. Cây động giảm storage với không gian thưa, đổi lại mỗi phép dịch địa chỉ cần tối đa 5 lookup, nhiều bảng nhỏ và overhead allocation. Peak có thể tăng trong mapping rồi rollback. Kernel page tables dùng chung nên thống kê kernel không được cộng lặp theo PID.

## 5. Quản lý RAM – `mm-memphy.c`

`init_memphy` tạo storage và danh sách free frame đầy đủ; device size nhỏ hơn một page có storage nhưng không có frame. `MEMPHY_get_freefp` chuyển node từ free sang used, trả FPN, zero frame. `MEMPHY_put_freefp` chỉ nhận FPN tồn tại trong used list, xóa owner, đưa lại free list theo thứ tự tăng dần. Điều này cũng giúp phát hiện double free. `MEMPHY_get_contiguous` tìm run các FPN liên tiếp, chỉ commit khi đủ số frame; zero toàn bộ run. `MEMPHY_owned/set_owner` kiểm tra/ghi chủ sở hữu mapping. Bounds check ngăn truy cập ngoài storage; sequential mode cập nhật cursor và vẫn bảo đảm đúng byte.

`mm_map_pages` precheck các PTE chưa map, lấy frame từng page (user) hoặc cả run (kernel), ghi PTE và owner. Nếu hết RAM giữa chừng, unmap các page đã thành công, trả frame và prune. Không cập nhật VMA/symbol trước khi thành công. Các helper thấp như MEMPHY, mm_map_pages, mm_translate giả định caller giữ `mm_lock` nếu được gọi đồng thời. DMA partner phải tuân thủ contract này hoặc xây dựng cơ chế pin/lock riêng.

## 6. ALLOC/FREE – `libmem.c` và `mm-vm.c`

Ví dụ `alloc 300 0`:

1. `liballoc` gửi syscall 17 nội bộ với region ID 0, size 300. Dispatcher xác thực PID, handler gọi `__alloc`.
2. `__alloc` kiểm tra size, reg ID, symbol chưa active. `get_free_vmrg_area` first-fit trên free list. Nếu thiếu, gửi `SYSMEM_INC_OP` để `inc_vma_limit` map số page cần (round up 4 KiB, trừ phần free tail nếu có).
3. `inc_vma_limit` xác nhận canonical user range và không overlap VMA khác; map qua `vm_map_ram` -> `mm_map_pages`. Nếu thành công cập nhật `vm_end`/`sbrk`, thêm free region mới. `sbrk` ở thiết kế này là break đã reserve theo page, còn từng allocation có kích thước byte chính xác.
4. First-fit cắt 300 byte từ free region, lưu symbol `rg_start=0`, `rg_end=300`, `mode_bit=1`. Zero vùng vừa cấp, cập nhật `regs[0]=0` (VA 0 hợp lệ).

Sau đó `alloc 100 1` có VA 300 và dùng cùng frame. Free list chứa `[400,4096)`. `free 0` đưa `[0,300)` về free list, xóa symbol; không trả frame user ngay vì có thể chia page với region khác. Free list được sắp theo địa chỉ và merge các region sát nhau. `alloc 100 2` tái dùng `[0,100)`. Mọi vùng dùng quy ước half-open `[start,end)`, nên offset == size không hợp lệ đối với byte read/write. Khi process exit, tất cả frame user được trả, kể cả page có vùng free.

`get_symrg_byid` dùng 10 register theo PDF p.5 dù storage symbol table skeleton có 30 slot; region ID tương ứng register cấp phát. `READ` lưu byte 0..255 vào destination register; nên chọn register chứa dữ liệu riêng (không dùng lại register đang giữ địa chỉ). Descriptor symbol vẫn là metadata quản lý region theo slot như framework gốc. Lỗi instruction được log và PC tiếp tục, không giả lập process exception phức tạp.

## 7. System Call và USR

### 7.1. Dispatcher và operation

`src/syscall.tbl` giữ syscall 0 listsyscall và 17 memmap; `syscalltbl.sh` sinh `src/syscalltbl.lst`. `_syscall` giữ recursive mutex, tra `mm_lookup(krnl,pid)` rồi dispatch. PID không tồn tại -> `-ESRCH`, syscall chưa đăng ký -> `-ENOSYS`, operation sai -> `-EINVAL`. `libsyscall` zero-init register arguments, không set `SC_INTERNAL`.

| a1 | Operation | Tham số / hành vi |
|---|---|---|
| 1 | MAP | a2=VA aligned, a3=số page; map thêm tại break hoặc zero range user đã map; từ chối kernel/hole/partial range. |
| 2 | INC | a2=VMA id, a3=số byte xin thêm; map page và cập nhật free region. |
| 3 | SWP | a2=RAM FPN, a3=SWAP FPN đã cấp/owner hợp lệ; copy primitive nội bộ. |
| 4 | IO_READ | a2=PA, trả byte ở a3; nội bộ, kiểm tra owner. |
| 5 | IO_WRITE | a2=PA, a3=byte; nội bộ, kiểm tra owner. |
| 6–12 | REGION_ALLOC/FREE/READ/WRITE, KMALLOC, COPY_FROM/TO | Extension riêng của implementation; chỉ wrapper nội bộ dùng để mọi memory instruction qua PID boundary. Không phải syscall number mới. |

`SC_INTERNAL` là cờ phân biệt wrapper tin cậy trong simulator; user instruction `syscall 17 4 ...` không có cờ và không thể tùy ý đọc RAM vật lý. `__sys_memmap` kiểm tra owner của frame và các helper kiểm tra mode/bounds. Syscall 0 vẫn dùng được dù RAM chỉ 2048 byte. Khi cần thêm syscall thật, thêm handler nhận `(struct krnl_t*, uint32_t pid, struct sc_regs*)`, cập nhật syscall.tbl + SYSCALL_OBJ, không truyền PCB trực tiếp.

### 7.2. User/kernel space

User lower canonical: `0x0000000000000000` tới `0x00ffffffffffffff`; kernel base mặc định `0xff00000000000000`, upper canonical tới `0xffffffffffffffff`. Khoảng giữa không canonical. Config có thể thêm một dòng base upper canonical, page-aligned, trước dòng RAM/SWP (demo chọn `0xff10000000000000`). Kernel dùng một `krnl->mm` chung, `krnl_pgd` là alias root đó. User có `proc->mm->pgd` độc lập. `mode_bit=1` user, `mode_bit=0` kernel.

`KMALLOC`: tính số page, tìm frame liên tiếp bằng MEMPHY_get_contiguous, map vào cây kernel chung, lưu region chính xác theo size byte trong symbol table của process chủ. Kernel VA tăng theo page; FREE kernel giải phóng frame và prune ngay, nhưng chưa tái sử dụng VA kernel cũ (bump allocator đơn giản). READ/WRITE thường yêu cầu mode=1; nếu reg trỏ kernel region thì fail. Process khác không có descriptor của region dù dùng chung cây kernel. `copy_from_user` yêu cầu src mode 1, dst mode 0; `copy_to_user` ngược lại. Cả hai kiểm tra toàn bộ range ở hai đầu trước khi copy từng byte tại cùng offset, kể cả khi qua page boundary. Không dùng DMA để giả lập CPU copy.

## 8. Đồng bộ và lifecycle

`mm_lock` là một recursive pthread mutex khởi tạo bằng pthread_once. Nó serialize toàn bộ transaction: registry, VMA, symbol, PTE, frame list, quyền, READ/WRITE, COPY, cleanup. Vì __alloc gọi INC qua syscall và access_byte gọi IO qua syscall nên cùng thread cần lock lặp; recursive tránh self-deadlock. Kết quả là dễ chứng minh đúng nhưng giảm parallelism, và copy lớn có thể giữ lock lâu. Không gọi scheduler trong khi giữ MM lock; scheduler có queue_lock riêng. CPU bỏ running list, unregister PID rồi thu hồi MM trước khi free PCB. Dispatcher giữ cùng MM lock từ lookup đến khi handler xong nên không dùng PCB đã free. Timer gốc dùng hai lock cho một predicate done; bản sửa dùng event_lock cho cả wait/update, atomic _time và timer_stop. Loader done dùng atomic.

Một race ví dụ: CPU0 và CPU1 đồng thời thấy free list đầu là FPN 0; không lock, cả hai map cùng frame, CPU0 WRITE 165 làm CPU1 READ thấy 165 dù khác process. Với lock, chuyển free->used và map là một transaction. Test 11: 4 pthread x 100 vòng alloc/write/read/free, kiểm tra kết quả riêng từng PID, tổng số frame used và tất cả frame được trả sau destroy.

## 9. Page Replacement – phạm vi chủ ý

PDF p.26: optional page replacement tùy giảng viên. Hiện không có thông báo nên không bật FIFO hay tự động swap-in/swap-out. Đã có init SWAP, frame list, PTE swapped encoding và `__swap_cp_page`/SWP primitive; test 09 xác nhận copy một frame đến SWAP. Không gọi `find_victim_page` tự động; không tuyên bố có eviction/page-in. Khi hết RAM, ALLOC/KMALLOC trả lỗi và rollback, không loop vô hạn. Với RAM 2048 byte, không có page 4096 byte nào, nên các ALLOC fail đúng thiết kế. Nếu sau này yêu cầu page replacement, cần victim queue toàn hệ thống (chỉ user, không kernel pinned), owner/PTE reverse mapping, swap slot allocation, update PTE, page fault/swap-in, giải phóng swap slot và synchronization. Không chỉ bật một macro để coi như xong.

## 10. Kiểm thử và đọc output

`make test-mm`: 11 nhóm test với assert (xem `tests/test_mm.c`), gồm canonical/index/PTE, FPN0/bounds, isolation, coalescing, kernel contiguous/copy/mode, PID/syscall, rollback, high address sparse/prune, SWP copy, CPU register, concurrency/cleanup.

`./os-mm os_mm_usr_demo`: hai process chạy cùng code, từng PID đọc 165 ở offset 4095 và 254 ở 4096 sau copy qua kernel; sau FREE/ALLOC lại, đọc 0 ở offset 4095. Không có `[INSTRUCTION-ERROR]`. Kết quả xen kẽ khác nhau do CPU scheduling. `[VMA]` show data segment + active/free region; `[PAGE TABLE]` show VA/PTE/FPN; `[MM-STATS]` show storage/access; `[INSTRUCTION-ERROR]` show PID, PC, opcode, rc. Log có thể lớn khi IODUMP/PAGETBL_DUMP bật trong os-cfg.h.

Các config gốc giữ byte-for-byte. p0s có READ tới register 20 (PDF p.5 chỉ cho 0..9) và WRITE/READ vùng 2/3 chưa ALLOC. m0s có write offset 1000 vào region 100 byte. m2s chứa DMA_MEMCPY, copy vào reg chưa cấp phát và copy ngược chiều; sc2/3 gọi syscall 101/440 chưa đăng ký. Những lỗi đó được báo, không sửa input để che lỗi. Một số run multi-CPU RAM nhỏ khác nhau số lỗi do thứ tự CPU; không dùng output mẫu làm golden byte-for-byte. Chỉ khẳng định các config gốc chạy tới khi kết thúc, không khẳng định mọi instruction thành công.

## 11. Trả lời vấn đáp (các câu hỏi liên quan)

- **Unified syscall interface ưu/nhược?** Thống nhất ABI, permission/error handling và logging, dễ thêm device và portable user API; nhược: overhead dispatch/context crossing, abstraction có thể không phù hợp thiết bị đặc thù, error semantics khó chuẩn hóa. Trong simulator, overhead chủ yếu lookup, lock, dispatch.
- **Long-running syscall?** Nếu giữ lock hoặc CPU quá lâu sẽ giảm responsiveness; hệ thật dùng blocking/wakeup, async I/O, interrupt, preemption/cancel. Không giữ MM lock khi đợi DMA hoặc queue. Bản hiện tại serialize CPU copy, không triển khai async DMA.
- **Segmentation + paging?** VMA/region theo logical object và permission, paging cấp phát frame rời rạc tránh external fragmentation RAM user; đổi lại internal fragmentation trong page cuối, page table overhead.
- **N-level paging?** Phân cấp address space rộng, cấp phát nhánh thưa theo nhu cầu; N cấp tăng lookup và metadata; TLB giảm cost nếu triển khai.
- **Paging vs contiguous?** Paging dễ allocate fragmented RAM và cách ly, có page walk overhead; contiguous dễ DMA/physical linear access, nhưng dễ external fragmentation, yêu cầu tìm run đủ lớn. Bản này user dùng paging, kernel dùng contiguous theo đề.
- **Không synchronization?** Trùng frame, corrupt free list, mất PTE, đọc/ghi ngoài lifetime PCB; ví dụ CPU0/CPU1 lấy cùng frame nêu mục 8.
- **Vì sao FPN 0 hợp lệ?** Bit present là dấu hiệu PTE có mapping; không dùng FPN 0 làm sentinel.
- **Vì sao 5 cấp chứ không 2^45 entry?** Flat table 128 TiB, cây sparse có 20 KiB cho một đường, có thể chia sẻ cấp trên cho nhiều page.
- **Vì sao FREE user không trả frame ngay?** Hai region có thể nằm cùng page; trả frame sẽ làm region còn sống mất dữ liệu. Coalesce free list để tái sử dụng.
- **Kernel space giống nhau thế nào?** Một root `krnl->mm->pgd` duy nhất cho tất cả process; symbol ownership kiểm tra quyền sử dụng từng kernel region.

## 12. Ghép code với nhóm / các giới hạn

1. Bản này dùng MM64/MLQ đang bật, không đảm bảo 32-bit deprecated hoặc non-paging legacy. VMA0 là data segment chính, không mô phỏng nhiều VMA thực tế. Parser chấp nhận cấu hình legacy không có dòng memory bằng default 1 MiB RAM + 16 MiB SWAP; cấu hình explicit ưu tiên. Không cần chỉnh macro MM_FIXED_MEMSZ để đọc testcase cũ.
2. Scheduler integration sử dụng quota MAX_PRIO-prio và FIFO per priority, giữ queue capacity 50 gốc. Thành viên Scheduler có thể thay phần thuật toán nhưng cần giữ `remove_running_proc` hoặc tương đương, không đăng ký/hủy PID sai thời điểm; registry độc lập với ready/running queue.
3. Co-processor: giữ Vdma_top, sysbus, dpi_rtl, dma_c_testbench gốc. `mm_translate` là API nội bộ cho DMA partner xây dựng PA/segment. Phải xử lý pinning/lifetime, bus width, async completion; không coi `libmem_dma_memcpy` là hoàn thành. Bản này trả ENOSYS cho instruction đó. Full `make os` cần Verilator tương thích model gốc, và kiểm thử thật trên VM.
4. 13-bit FPN, kernel virtual bump, no TLB, no page replacement. Có thể nâng cấp sau khi nhóm thống nhất. Cần ghép báo cáo Scheduler và DMA với `docs/report_mm.tex`, điền thông tin nhóm và kết quả đo trên VM trước khi nộp.
5. Tài liệu giải thích là phần học tập: hãy tự chạy các test, đặt breakpoint tại `__sys_memmap`, `mm_map_pages`, `walk`, `access_byte`, `mm_copy_region` và theo dõi PID/VA/PTE/FPN để có thể trình bày khi demo.
