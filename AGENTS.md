# AGENTS.md — Quy tắc bắt buộc cho AI agent

> File này dành cho **mọi AI agent** (Claude Code, Cursor, Copilot, Codex, Gemini...) làm việc trong repo này.
> Nếu công cụ của bạn đọc `CLAUDE.md` / `.cursorrules` / `GEMINI.md`, hãy coi file này là nội dung chuẩn (có thể symlink: `ln -s AGENTS.md CLAUDE.md`).
> **Đọc xong `AGENTS.md` và `spec.md` rồi mới được viết bất kỳ dòng code nào.**

---

## 0. Bối cảnh trong 5 dòng

- Dự án: **Simple Operating System (OSSIM)** — bài tập lớn môn Operating Systems (CO2018, HCMUT), viết bằng **C**, mô phỏng OS gồm **scheduler MLQ**, **paging 32/64-bit + swap + tách user/kernel**, **system call**, **đồng bộ hoá**, **co-processor DMA** (DMA thật qua verilator).
- Nhóm 4 người. Chấm: demo 7đ + báo cáo 3đ.
- Nguồn sự thật duy nhất: **`spec.md`** (bản diễn giải của đặc tả PDF gốc). Nếu `spec.md` và code starter mâu thuẫn → **hỏi**, đừng chọn bừa.

---

## 1. QUY TẮC VÀNG: KHÔNG BỊA — KHÔNG ĐOÁN — PHẢI HỎI

1. **Nếu yêu cầu, đặc tả, hành vi mong đợi, tên hàm, chữ ký hàm, cấu trúc file, định dạng input/output không rõ → DỪNG và HỎI người dùng.** Không tự điền khoảng trống rồi triển khai.
2. **Cấm bịa thông tin**: không tự tạo tên hàm/struct/macro/file/syscall number/tham số "nghe có vẻ đúng" mà không thấy trong `spec.md` hoặc trong code hiện có. Nếu cần tạo mới, hãy **nêu rõ đó là đề xuất** và xin xác nhận.
3. Mục **§13 của `spec.md`** liệt kê các điểm mơ hồ đã biết. Mọi việc đụng tới các điểm đó **bắt buộc hỏi trước**.
4. Khi hỏi: nêu **(a)** bạn đang làm gì, **(b)** điểm nào không rõ, **(c)** 2–3 phương án khả dĩ kèm ưu/nhược, **(d)** phương án bạn đề xuất. Mỗi lần hỏi gọn, tập trung vào quyết định cần có.
5. Không biết thì nói "mình chưa biết / chưa thấy trong spec hoặc code". Không suy diễn nội dung file bạn chưa đọc. Không khẳng định "đã chạy / đã test" nếu thực tế chưa chạy.
6. Không tự "giả định hợp lý" rồi làm xong mới báo. Giả định chỉ được dùng khi **người dùng đã duyệt**, và phải ghi lại ở §Decision log (README hoặc `docs/decisions.md`).

---

## 2. Phạm vi & ranh giới thay đổi

- **Chỉ làm đúng phần được giao.** Không refactor, đổi tên, format lại file ngoài phạm vi.
- **File "obsolete" — KHÔNG xoá, KHÔNG phụ thuộc vào**: `mem.h`, `loader.h`, `mem.c`, `paging.c`, `run_queue`/`processed_queue`, `pcb_t.page_table`. Giữ nguyên để tránh lỗi khó truy.
- **Không sửa** `input/` và `output/` mẫu của giảng viên. Test mới của nhóm đặt ở thư mục riêng (VD `input_team/`, `tests/`).
- **Không đổi** chữ ký hàm/struct trong header starter nếu chưa được duyệt (nhiều module phụ thuộc nhau).
- Tính năng nào cũng **cô lập bằng cờ** trong `include/os-cfg.h` (`MLQ_SCHED`, `MM_PAGING`, `MM_FIXED_MEMSZ`, `MM64`...). Không hard-code làm hỏng cấu hình khác. Khi chỉ làm scheduler, giữ mặc định các cờ memory.
- Không thêm dependency/thư viện ngoài khi chưa được duyệt.
- Không đụng vào `.git`, CI, cấu hình môi trường của người khác.

---

## 3. Ràng buộc kỹ thuật BẮT BUỘC (từ đặc tả)

| # | Ràng buộc |
|---|---|
| R1 | **Scheduler MLQ**: mỗi priority = 1 `ready_queue`; prio nhỏ = ưu tiên cao; `slot = MAX_PRIO - prio`; hết slot thì chuyển queue kế, phần còn lại để lượt sau. Round-robin trong queue; process hết time slice quay về **đúng queue của nó**. Không cài feedback. |
| R2 | Hàm phải cài: `enqueue()`, `dequeue()` (`queue.c`), `get_proc()` (`sched.c`). |
| R3 | **Dual priority**: priority trong file cấu hình mô phỏng **ghi đè** priority mặc định trong file chương trình (`prio` vs `priority`). |
| R4 | **Tách user/kernel space.** Không truy cập trực tiếp từ user space qua PCB; chỉ dùng `struct krnl_t`. Giao tiếp qua syscall **truyền PID, không truyền con trỏ PCB**. |
| R5 | `READ`/`WRITE` chỉ chạm địa chỉ user. Truy cập địa chỉ kernel = invalid memory operation. User↔kernel dùng `copy_from_user`/`copy_to_user`. |
| R6 | `KMALLOC` cấp vùng **liên tục về vật lý** ở kernel space. |
| R7 | `FREE` không trả frame vật lý; đưa region vào `vm_freerg_list`. |
| R8 | `ALLOC` hết chỗ trong free list → tăng `sbrk` qua syscall `memmap` (`SYSMEM_INC_OP` → `inc_vma_limit`), rồi map frame vào page table. |
| R9 | Swap: 1 RAM + tối đa 4 SWAP; READ/WRITE yêu cầu page ở RAM (swap in/out khi cần). |
| R10 | 64-bit: **5-level paging** (PGD/P4D/PUD/PMD/PT, 9 bit mỗi tầng, offset 12 bit) + canonical address. Phải thống kê **số lần truy cập bộ nhớ** và **dung lượng bảng trang**. |
| R11 | Kernel space phải **giống hệt nhau cho mọi process**. |
| R12 | **Mọi tài nguyên dùng chung** (ready queues, running list, page table/mm, free/used frame lists, SWAP, `vm_freerg_list`, PID generator, DMA channel) phải được bảo vệ bằng lock; mỗi lock phải có comment nói bảo vệ gì và thứ tự lấy lock (tránh deadlock). |
| R13 | Co-processor: tương tác **bất đồng bộ** CPU↔DMA (kiểu interrupt). Demo/presentation dùng **DMA thật (verilator)**; `dma_copy()` software chỉ để dev/test. Xử lý dữ liệu **từng byte** (tránh lỗi endianness). `ADDR_WIDTH=64`, `DATA_WIDTH=32`. |
| R14 | Syscall mới: tạo `src/sys_<name>.c` + thêm vào `Makefile` (`SYSCALL_OBJ`) + dòng trong `src/syscall.tbl`. Hiện có: `0 listsyscall`, `17 memmap`. |
| R15 | Output **không cần trùng 100%** mẫu (loader & scheduler chạy đồng thời) nhưng phải **giải thích được và khớp lý thuyết**. |

---

## 4. Chuẩn code

- Ngôn ngữ: **C**, biên dịch bằng `make all`. Coding style: **GNU C** (https://www.gnu.org/prep/standards/html_node/Writing-C.html): thụt lề 2 space theo kiểu GNU, hàm khai báo kiểu `int\nfunc (args)` nếu repo starter đang theo; **nếu code starter dùng style khác, bám theo starter trong file đó và hỏi người dùng**.
- Comment, tên biến/hàm, commit message: **tiếng Anh**. Giải thích với người dùng: **tiếng Việt**.
- Mỗi hàm công khai có comment mô tả: mục đích, tham số, giá trị trả về, lỗi, thread-safety.
- Luôn **kiểm tra giá trị trả về**, `NULL`, biên mảng, tràn số; xử lý lỗi rõ ràng (không `exit` bừa trong kernel code).
- Không memory leak: mọi `malloc` phải có đường `free` tương ứng; kiểm tra bằng `valgrind` / `-fsanitize=address,undefined` khi có thể.
- Biên dịch sạch với `-Wall -Wextra`; không bỏ qua warning mà không giải thích.
- Không dùng magic number; dùng `#define`/`enum` có tên (kích thước page, số bit từng tầng, mask PTE...).
- Không để `printf` debug rác trong code nộp; dùng macro debug bật/tắt bằng cờ.
- Không hard-code đường dẫn tuyệt đối máy cá nhân.

---

## 5. Quy trình làm việc cho mỗi task

1. **Đọc**: `spec.md` (phần liên quan) + code hiện có liên quan. Nếu chưa đọc file nào thì nói rõ.
2. **Xác nhận hiểu**: nêu ngắn gọn bạn hiểu task là gì; nếu có điểm mơ hồ → hỏi (§1).
3. **Kế hoạch ngắn**: file sẽ sửa, hàm sẽ thêm/đổi, rủi ro. Với thay đổi lớn, chờ duyệt.
4. **Triển khai nhỏ, tăng dần**: mỗi bước build được (`make all`) trước khi sang bước sau.
5. **Kiểm thử**: chạy input mẫu trong `input/`, so với `output/`; thêm test riêng cho case biên. Báo cáo **chính xác** những gì đã chạy và kết quả thực tế (kể cả fail).
6. **Báo cáo kết quả**: liệt kê file đổi, hàm đổi, cách kiểm tra, điểm còn tồn đọng/giả định.
7. **Không tự commit/push/merge** nếu người dùng chưa yêu cầu.

### Kiểm thử tối thiểu theo module

| Module | Cần test |
|---|---|
| queue/scheduler | queue rỗng/đầy, nhiều priority, 1 vs nhiều CPU, process cùng priority, slot cạn kiệt, race loader↔scheduler |
| paging | alloc/free/read/write, alloc vượt RAM → swap, swap in/out, free rồi alloc lại tái sử dụng region, địa chỉ biên |
| user/kernel | READ/WRITE vào địa chỉ kernel bị từ chối; copy_from/to_user đúng; syscall truyền PID |
| 64-bit | dịch địa chỉ từng tầng, ví dụ trong `spec.md §7.5`, thống kê truy cập/size |
| DMA | `dma_memcpy` đúng nội dung, bất đồng bộ (CPU không bị block), lỗi `ch_error`, offset/size biên |
| sync | chạy nhiều CPU, lặp nhiều lần, thử `-fsanitize=thread` / `helgrind` |

---

## 6. Bảo mật & liêm chính học thuật

- Mã nguồn starter thuộc giảng viên; chỉ dùng cho môn CO2018 tại HCMUT. **Không đưa code ra ngoài**, không upload lên dịch vụ công khai, không nhúng secret/token vào repo.
- **Không sao chép bài** từ nhóm/nguồn khác; nếu tham khảo tài liệu (Linux kernel, OSTEP...) phải ghi nguồn trong báo cáo, và tự viết lại.
- Mọi đoạn code do AI sinh ra là **đề xuất**; thành viên nhóm phải đọc hiểu và giải thích được khi vấn đáp/demo. Hãy viết code **dễ giải thích**, ưu tiên rõ ràng hơn "thông minh".
- Báo cáo phải do nhóm hiểu và tự viết; agent chỉ hỗ trợ cấu trúc, soát lỗi, gợi ý.

---

## 7. Giao tiếp

- Trả lời bằng **tiếng Việt**, ngắn gọn, đúng trọng tâm; thuật ngữ kỹ thuật giữ tiếng Anh.
- Khi không chắc: nói thẳng mức độ chắc chắn. Khi phản đối một yêu cầu vì lý do kỹ thuật: nêu lý do và phương án thay thế.
- Không "tô hồng" kết quả. Báo lỗi, test fail, phần chưa làm một cách thẳng thắn.
- Không đưa ra tuyên bố về điểm số/kết quả chấm của giảng viên.

---

## 8. Checklist trước khi nói "xong"

- [ ] `make all` sạch (không lỗi, warning đã xử lý/giải thích)
- [ ] Đã chạy input mẫu liên quan và so với `output/`
- [ ] Không phá cấu hình cờ khác trong `os-cfg.h`
- [ ] Không đụng file ngoài phạm vi task / file obsolete
- [ ] Tài nguyên dùng chung đã có lock + comment
- [ ] Không leak, không truy cập ngoài biên
- [ ] Đã liệt kê giả định và điểm cần người dùng xác nhận
- [ ] Đã cập nhật phần liên quan trong README/báo cáo nếu hành vi thay đổi
