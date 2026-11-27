# Kết quả kiểm thử thực tế v3 — Student Friendly Implementation

Môi trường Linux, GCC, pthread, Clang AST. Chạy lại trên chính source v3, không dùng log cũ của v2. Chạy trong thư mục root dự án:

```bash
make os-mm
make test-mm
make test-mm MM_TEST_FLAGS='-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie'
./os-mm os_mm_usr_demo
```

| Kiểm tra | Kết quả |
|---|---|
| `make os-mm` | Thành công |
| `make test-mm` | 12/12 nhóm assertion PASS |
| ASan + UBSan | 12/12 nhóm PASS, không có báo cáo sanitizer |
| Demo 2 CPU / 2 process | Exit 0, không có instruction error |
| 14 config gốc | 14/14 exit 0, không đồng nghĩa mọi instruction hợp lệ |
| Full `make os` | Exit 2: thiếu `verilated.h` khi biên dịch RTL |
| Prototype và comment | 99/99 định nghĩa v2 giữ kiểu; 98/98 hàm gốc giữ kiểu; 99/99 có comment |
| Input/output gốc | 43/43 byte-identical |
| Unit test source | Giữ nguyên `tests/test_mm.c` của v2 |

## Nhóm test

1. Canonical VA, chỉ số 5 cấp, PTE, frame số 0 và swap encoding.
2. ALLOC nhiều page, boundary, dirty, bounds, thống kê 5+1.
3. Hai process cùng VA nhưng khác FPN và dữ liệu.
4. FREE, coalescing, first-fit, zero-fill, từ chối double-free.
5. KMALLOC contiguous, kernel table chung, mode, copy hai chiều.
6. PID sai, syscall lạ, MAP, từ chối raw physical IO.
7. Hết RAM: rollback frame/VMA/symbol.
8. Địa chỉ cao, đọc/ghi 64 bit, nhánh sparse, prune, canonical.
9. SWAP copy primitive, kiểm tra bounds.
10. CPU READ ghi register đích, từ chối reg 20.
11. 4 pthread x 100 vòng ALLOC/WRITE/READ/FREE, thu hồi toàn bộ frame.
12. RAM phân mảnh: ALLOC thành công, KMALLOC liên tục thất bại đúng, từ chối tràn tham số 32-bit.

## Input gốc và demo

```text
os_0_mlq_paging exit=0 errors=10
os_1_mlq_paging exit=0 errors=6
os_1_mlq_paging_small_1K exit=0 errors=23
os_1_mlq_paging_small_4K exit=0 errors=17
os_1_singleCPU_mlq exit=0 errors=1
os_1_singleCPU_mlq_paging exit=0 errors=1
os_2_mlq_paging exit=0 errors=8
os_2_singleCPU_mlq_paging exit=0 errors=8
os_mm_usr_demo exit=0 errors=0
os_sc exit=0 errors=1
os_syscall exit=0 errors=5
os_syscall_list exit=0 errors=0
sched exit=0 errors=0
sched_0 exit=0 errors=0
sched_1 exit=0 errors=0
```

`errors` là số dòng `[INSTRUCTION-ERROR]`. Các input gốc chứa cả trường hợp thiếu RAM, region/register không hợp lệ, offset vượt biên, syscall chưa đăng ký hoặc DMA ngoài phạm vi. Không thay đổi input/output gốc để ép test pass. Không so sánh byte-for-byte output thực thi với file mẫu vì Scheduler đa CPU và định dạng thống kê đã thay đổi.

Demo: mỗi process đọc 165 ở offset 4095, 254 ở offset 4096 sau copy kernel; đọc 0 sau tái cấp phát. Kernel cuối cùng chỉ còn PGD 4096 byte, peak 20480 byte, walks=8, pt_access=40, data_access=8, total=48. Mỗi user trong demo: 20480 byte, walks=18, pt_access=90, data_access=24588, total=24678.

Log thực tế: `tests/results/`. Cần chạy lại trên VM có Verilator và khi ghép với branch nhóm. Không có commit/push lên GitHub từ phiên này.

## Đối chiếu với v2

Số lỗi instruction của cả 15 config (14 gốc + demo) giống v2; 12 nhóm assertion vẫn PASS, không có báo cáo ASan/UBSan. Giá trị và thống kê của demo khớp v2. Điều này là kiểm thử hồi quy theo bộ test có sẵn, không phải chứng minh mọi hành vi đều giống nhau. Build CPU-only có 4 warning unused parameter từ `src/mem.c` legacy không sửa; không có lỗi biên dịch. Full DMA/Verilator chưa kiểm thử được. Các file Scheduler/Timer chỉ thay comment; không sửa thuật toán của v2.

Lệnh thử full build: `make os`; log `tests/results/full-build.log`. Log unit, sanitizer, 15 config, AST audit đều được tạo lại. Không thay đổi repository GitHub.
