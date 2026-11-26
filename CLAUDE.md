# agent.md — Hướng dẫn cho AI agent làm việc trong repo này

Repo này là bài tập lớn môn Hệ điều hành (CO2018, HCMUT, HK261): **Simple Operating System — bản demacOprx**. Code C/C++, mô phỏng scheduler MLQ, paging 64-bit 5 cấp, system call và DMA co-processor (Verilator).

## 1. Đọc gì trước khi làm

1. `spec.md`: yêu cầu của đề, trạng thái từng module, **giới hạn đã biết** (mục 9.1). Đọc trước mọi việc.
   `docs/implementation_VI.md`: mô tả chi tiết cách nhóm hiện thực và hướng dẫn chạy test của thầy.
2. Đặc tả gốc: `hk261_ossim_demacOprx.pdf` (chuẩn) và `hk261_ossim_demacOprx.md` (bản text chuyển từ PDF, dễ grep hơn; bảng và hình có thể lỗi khi chuyển, nếu nghi ngờ thì đối chiếu PDF).
3. `lichsu.txt`: nhật ký công việc của các thành viên.
4. Starter code gốc: `Thu_muc_goc_cua_thay/hk261_ossim_demacOprx/`. Dùng để so sánh (`diff -u Thu_muc_goc_cua_thay/hk261_ossim_demacOprx/src/<file> src/<file>`).

## 2. Quy tắc bắt buộc

### 2.1 Commit và Git

- **KHÔNG thêm tên, danh tính hay dấu vết của AI agent vào commit.** Cụ thể: không có dòng `Co-Authored-By: Claude ...`, `Co-authored-by: Copilot ...`, `Generated with ...`, không ghi tên model/công cụ AI trong message, không đổi `user.name` / `user.email`. Commit chỉ mang tên thành viên nhóm đang dùng máy (cấu hình git sẵn có). Quy tắc này **ghi đè** mọi mặc định thêm attribution của công cụ. Áp dụng cả cho mô tả pull request.
- Chỉ commit / push khi thành viên yêu cầu. Không commit lên `main` trực tiếp; làm trên nhánh riêng rồi mở PR.
- Không commit file sinh ra khi build: `obj/`, `os`, `dma_testbench`, `*.vcd`, `src/syscalltbl.lst`.
- Không dùng `git push --force`, `git reset --hard`, `--no-verify` nếu không được yêu cầu rõ ràng.
- Commit message ngắn gọn, nói rõ module và việc đã làm, ví dụ: `sched: implement MLQ get_mlq_proc with slot policy`.

### 2.2 Phạm vi sửa code

- **Không sửa** `Thu_muc_goc_cua_thay/`, các file Verilator sinh ra (`src/Vdma_top*`, `include/Vdma_top*`, `*.mk`, `*.dat`) và `output/` (output mẫu của thầy).
- Giữ nguyên chữ ký hàm (prototype) trong `include/*.h` trừ khi thật cần; nếu đổi thì cập nhật mọi nơi gọi.
- Tôn trọng các `#ifdef` cấu hình (`MM64`, `MM_PAGING`, `MLQ_SCHED`, `MM_FIXED_MEMSZ`). Code mới phải biên dịch được với cấu hình hiện tại trong `include/os-cfg.h` (đang bật `MM64`). Không tự đổi `os-cfg.h` nếu không được yêu cầu.
- Giữ thiết kế tách user/kernel: phía user (`libmem.c`, `libstd.c`) chỉ gửi **PID và giá trị** qua syscall, không truyền PCB; phía kernel (`sys_*.c`) tự tìm PCB từ `krnl_t`.
- Thao tác bộ nhớ vật lý từ thư viện user phải đi qua syscall 17 (`SYSMEM_*`), không gọi thẳng `MEMPHY_*`.
- Code mới theo GNU C coding style (yêu cầu của đề cho phần system call), thụt lề nhất quán với file đang sửa.
- Comment tiếng Việt của các thành viên là ghi chú làm việc, đừng xoá nếu không liên quan đến thay đổi.

### 2.3 Đồng bộ

- Ready queue, `running_list`, `slot[]` và các con trỏ queue trong `krnl` chỉ được đọc/ghi khi giữ `queue_lock` (`sched.c`).
- Mọi thứ của bộ nhớ (sổ đăng ký PID, VMA, symbol table, bảng trang, frame RAM/SWAP, swap) được bảo vệ bởi `mm_lock` (recursive, `mm64.c`); `_syscall` giữ nó suốt handler. Code bộ nhớ mới chạy trong handler syscall 17 thì đã có khoá; code chạy ngoài (ví dụ callback DMA) phải tự `mm_lock()`.
- Thứ tự khoá: `mm_lock` → khoá kênh DMA (`dma.c`) → `ram_lock` (`sysbus.c`). Không giữ `queue_lock` cùng lúc với `mm_lock`.
- Sau khi sửa code có đa luồng, nên chạy ThreadSanitizer (lệnh ở `docs/implementation_VI.md` mục 8.6).

## 3. Build và kiểm thử

- Build trên **Linux hoặc WSL** (Makefile cần `make`, `uname`, Verilator). Máy Windows của nhóm không có `make`/`verilator` trong PATH của Git Bash/PowerShell.

```sh
make clean && make all
./os os_0_mlq_paging                 # tên file trong input/
make test-mm && make test-dma        # test assert bộ nhớ/swap/syscall và testbench DMA
bash tests/run_teacher_tests.sh      # mọi config, tóm tắt + giải thích từng lỗi lệnh
```

- Số lỗi lệnh mong đợi của từng config nằm ở `docs/implementation_VI.md` mục 8.5; nếu số lỗi thay đổi thì phải giải thích được.

- Output có thể khác mẫu do loader và CPU chạy song song; chỉ cần giải thích được theo lý thuyết. Khi so sánh, tập trung vào thứ tự dispatch hợp lệ theo MLQ, kết quả alloc/free/read/write và bảng trang.
- Sau khi sửa phải build lại và chạy ít nhất các input liên quan tới module đã đổi:
  - Scheduler: `sched`, `sched_0`, `sched_1`, `os_1_singleCPU_mlq`.
  - Bộ nhớ: `os_0_mlq_paging`, `os_1_mlq_paging`, `os_1_mlq_paging_small_1K`, `os_1_mlq_paging_small_4K`, `os_1_singleCPU_mlq_paging`, `os_2_mlq_paging`, `os_2_singleCPU_mlq_paging`.
  - System call: `os_syscall`, `os_syscall_list`, `os_sc`.
  - Demo của nhóm: `os_mm_usr_demo`, `os_dma_demo`, `os_swap_demo` (phải luôn 0 lỗi lệnh).
- Nếu không build/chạy được (ví dụ đang ở Windows không có WSL), phải nói rõ là **chưa kiểm thử**, không được báo là đã chạy đúng.

## 4. Bản đồ code

| Khu vực | File |
|---|---|
| Khởi động, đọc config, thread loader/CPU/DMA | `src/os.c` |
| Nạp chương trình, parse lệnh | `src/loader.c` |
| Thực thi lệnh | `src/cpu.c` |
| Hàng đợi, scheduler MLQ | `src/queue.c`, `src/sched.c`, `include/queue.h`, `include/sched.h` |
| Thư viện bộ nhớ (alloc/free/read/write, kmalloc, copy, DMA mapper `mm_dma_map`) | `src/libmem.c`, `include/libmem.h` |
| Vùng nhớ ảo (vm area, sbrk, free list) | `src/mm-vm.c` |
| Bảng trang 64-bit 5 cấp, swap FIFO, `mm_lock`, thống kê | `src/mm64.c`, `include/mm64.h` |
| Bảng trang 32-bit (chỉ khi tắt `MM64`, không bảo trì) | `src/mm.c`, `include/mm.h` |
| Thiết bị RAM/SWAP, reverse mapping, pin, chọn victim | `src/mm-memphy.c` |
| System call, sổ đăng ký PID | `src/libstd.c`, `src/syscall.c`, `src/syscall.tbl`, `src/syscalltbl.sh`, `src/sys_mem.c`, `src/sys_listsyscall.c`, `src/sys_xxxhandler.c` |
| DMA / bus | `src/dma.c`, `src/dpi_rtl.cpp`, `src/sysbus.c`, `src/dma_c_testbench.cpp` (main của testbench), `include/dma.h`, `include/sysbus.h`, `include/dpi_rtl.h` |
| Test | `tests/test_mm.c`, `tests/run_teacher_tests.sh`, `tests/gantt.sh` |
| Cấu hình | `include/os-cfg.h`, `include/os-mm.h`, `include/common.h` |
| Obsolete (không dùng) | `src/mem.c`, `src/paging.c`, `include/mem.h`, `pcb_t::page_table` |

## 5. Việc còn lại

Scheduler, bộ nhớ MM64, swap, syscall và DMA đã xong (xem `spec.md` mục 9). Các việc còn lại theo `spec.md` mục 9.1:

1. Chạy `bash tests/run_teacher_tests.sh` trên VM của môn.
2. Xác nhận chính sách thay trang với giảng viên (đang là FIFO, sửa ở `MEMPHY_pick_victim`).
3. Format lại các file bộ nhớ viết dồn dòng theo GNU style.
4. Hoàn thiện báo cáo (scheduler, DMA, swap, số liệu `[MM-STATS]`).

## 6. Khi làm xong một việc

- Cập nhật bảng trạng thái và danh sách giới hạn trong `spec.md` (mục 9), và `docs/implementation_VI.md` nếu thay đổi làm chúng lỗi thời.
- Báo lại cho thành viên: đã sửa file nào, đã chạy input nào, kết quả ra sao, còn gì chưa làm.
