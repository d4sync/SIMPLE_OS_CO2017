# Hướng dẫn chạy Simple OS trên máy ảo VMware (từ A tới Z)

Tài liệu dành cho mọi thành viên trong nhóm, kể cả người chưa dùng Linux bao giờ. Làm lần lượt từ trên xuống; mỗi bước đều có lệnh để gõ và kết quả mong đợi để tự kiểm tra.

**Tổng thời gian lần đầu:** khoảng 1–1,5 giờ (phần lớn là chờ tải và cài Ubuntu). Các lần sau chỉ mất vài phút.

## Mục lục

- [0. Chuẩn bị](#0-chuẩn-bị)
- [1. Cài VMware Workstation](#1-cài-vmware-workstation)
- [2. Tạo máy ảo Ubuntu](#2-tạo-máy-ảo-ubuntu)
- [3. Cài công cụ build trong Ubuntu](#3-cài-công-cụ-build-trong-ubuntu)
- [4. Lấy code từ GitHub](#4-lấy-code-từ-github)
- [5. Build](#5-build)
- [6. Chạy test của thầy](#6-chạy-test-của-thầy)
- [7. Chạy test riêng của nhóm](#7-chạy-test-riêng-của-nhóm)
- [8. Lấy số liệu cho báo cáo](#8-lấy-số-liệu-cho-báo-cáo)
- [9. Làm việc hằng ngày](#9-làm-việc-hằng-ngày)
- [10. Xử lý lỗi thường gặp](#10-xử-lý-lỗi-thường-gặp)
- [11. Checklist trước khi demo](#11-checklist-trước-khi-demo)
- [Phụ lục A. Cài Verilator 5.032 từ mã nguồn](#phụ-lục-a-cài-verilator-5032-từ-mã-nguồn)
- [Phụ lục B. Tóm tắt lệnh](#phụ-lục-b-tóm-tắt-lệnh)

> **Đã có sẵn máy ảo của môn** (file `.vmx` hoặc `.ova` do thầy/nhóm đưa)? Mở VMware → **File → Open** → chọn file đó → bật máy, rồi nhảy thẳng tới [mục 3](#3-cài-công-cụ-build-trong-ubuntu). Ở mục 3 nhớ kiểm tra phiên bản Verilator.

---

## 0. Chuẩn bị

| Cần có | Yêu cầu |
|---|---|
| Máy tính | Windows 10/11 64-bit, RAM ≥ 8 GB, ổ cứng trống ≥ 30 GB |
| Ảo hoá | Bật Intel VT-x / AMD-V trong BIOS (đa số máy đã bật sẵn) |
| Tài khoản | Tài khoản GitHub đã được mời vào repo `d4sync/SIMPLE_OS_CO2017` (nếu repo để private) |
| Mạng | Tải khoảng 6 GB (VMware + Ubuntu + gói cài) |

**Môi trường nhóm đã kiểm tra chạy đúng:** Ubuntu 26.04 LTS, gcc 15.2, GNU Make 4.4.1, **Verilator 5.032**. Nên dùng đúng Ubuntu 26.04 LTS để `apt` cài ra đúng Verilator 5.032.

---

## 1. Cài VMware Workstation

1. Vào trang chính thức của VMware/Broadcom, tải **VMware Workstation Pro** cho Windows. Bản này miễn phí cho cá nhân và học tập, nhưng phải đăng ký tài khoản Broadcom để tải.
2. Chạy file cài, bấm **Next** đến hết, giữ các lựa chọn mặc định. Nếu được hỏi về *Windows Hypervisor Platform (WHP)* thì cứ đồng ý.
3. Khởi động lại máy nếu được yêu cầu.

> Máy đã bật WSL2, Docker Desktop hoặc Hyper-V vẫn chạy được VMware (bản 17 trở lên), chỉ chậm hơn một chút. Không cần tắt WSL.

---

## 2. Tạo máy ảo Ubuntu

### 2.1 Tải Ubuntu

Vào https://ubuntu.com/download/desktop, tải **Ubuntu 26.04 LTS Desktop** (file `.iso`, khoảng 6 GB).

### 2.2 Tạo máy ảo

1. Mở VMware → **Create a New Virtual Machine**.
2. Chọn **Typical** → **Next**.
3. Chọn **Installer disc image file (iso)** → **Browse** → chọn file `.iso` vừa tải → **Next**.
   - Nếu VMware hiện **Easy Install**: điền tên, username, password (ghi nhớ password này) → **Next**.
   - Nếu không hiện Easy Install: cứ **Next**, lát nữa sẽ tạo user trong bộ cài Ubuntu.
4. Đặt tên máy ảo (ví dụ `OSSIM-Ubuntu`) → **Next**.
5. Disk size: **30 GB**, chọn **Store virtual disk as a single file** → **Next**.
6. Bấm **Customize Hardware…**:
   - **Memory:** 4096 MB (máy 16 GB RAM có thể để 6144 MB).
   - **Processors:** Number of processors = 1, Number of cores = **2 đến 4**. Test có cấu hình 4 CPU, nên nhiều core sẽ chạy mượt hơn.
   - Bấm **Close** → **Finish**.

### 2.3 Cài Ubuntu

Máy ảo tự bật và vào bộ cài Ubuntu (nếu dùng Easy Install thì nó tự cài, chỉ việc chờ):

1. Chọn ngôn ngữ (nên để **English** cho dễ đọc thông báo lỗi) → **Install Ubuntu**.
2. Keyboard: **English (US)** → **Next**.
3. Kết nối mạng: để mặc định.
4. **Interactive installation** → **Default selection** → **Next**.
5. **Erase disk and install Ubuntu** → **Next**. Thao tác này chỉ xoá ổ ảo 30 GB vừa tạo, **không đụng tới ổ Windows**.
6. Tạo user: tên, username (viết thường, không dấu, ví dụ `nhom1`) và password → **Next**.
7. Chọn múi giờ → **Install**. Chờ 10–20 phút.
8. Bấm **Restart now**. Nếu màn hình báo *"Please remove the installation medium"* thì cứ nhấn **Enter**.
9. Đăng nhập bằng password vừa tạo.

### 2.4 Cho phép copy/paste giữa Windows và máy ảo (nên làm)

Mở Terminal trong Ubuntu (phím tắt **Ctrl + Alt + T**) và gõ:

```sh
sudo apt update
sudo apt install -y open-vm-tools-desktop
sudo reboot
```

> Mỗi lần `sudo` hỏi password, gõ password của user Ubuntu rồi Enter. Khi gõ, màn hình **không hiện ký tự gì**, đó là bình thường.

Sau khi khởi động lại, bạn có thể copy lệnh từ tài liệu này bên Windows rồi dán vào Terminal bằng **Ctrl + Shift + V**.

---

## 3. Cài công cụ build trong Ubuntu

Mở Terminal (**Ctrl + Alt + T**) và gõ:

```sh
sudo apt update
sudo apt install -y build-essential git verilator
```

Kiểm tra phiên bản:

```sh
gcc --version | head -1
make --version | head -1
verilator --version
```

Kết quả mong đợi (trên Ubuntu 26.04):

```text
gcc (Ubuntu 15.2.0-...) 15.2.0
GNU Make 4.4.1
Verilator 5.032 2025-01-01 rev (Debian 5.032-1)
```

**Nếu Verilator không phải 5.032**, ví dụ máy ảo của môn hoặc Ubuntu bản cũ hơn (22.04, 24.04 cho ra Verilator cũ hơn), xử lý như sau:

- Verilator **4.x**: quá cũ, chắc chắn không build được phần DMA. Hãy cài 5.032 theo [Phụ lục A](#phụ-lục-a-cài-verilator-5032-từ-mã-nguồn).
- Verilator **5.x khác 5.032**: cứ thử build ở mục 5. Nếu lỗi trong các file `Vdma_top*` thì cài 5.032 theo Phụ lục A.

---

## 4. Lấy code từ GitHub

### 4.1 Repo public hoặc đã có quyền truy cập

```sh
cd ~
git clone -b tong_hop_code https://github.com/d4sync/SIMPLE_OS_CO2017.git
cd SIMPLE_OS_CO2017
```

### 4.2 Nếu repo để private

Khi `git clone` hỏi:

- **Username:** tên tài khoản GitHub của bạn.
- **Password:** **không** nhập mật khẩu GitHub, mà nhập **Personal Access Token**. Cách tạo token:
  1. Đăng nhập GitHub trên trình duyệt → ảnh đại diện → **Settings**.
  2. **Developer settings** → **Personal access tokens** → **Tokens (classic)** → **Generate new token (classic)**.
  3. Đặt tên, chọn hạn dùng, tích ô **repo** → **Generate token**.
  4. Copy token (bắt đầu bằng `ghp_…`) và lưu lại, vì GitHub chỉ hiện một lần.

Để khỏi phải nhập token mỗi lần, chạy lệnh sau **trước khi** clone:

```sh
git config --global credential.helper store
```

Cài đặt tên commit (mỗi người một lần, dùng tên thật của mình):

```sh
git config --global user.name "Ten Cua Ban"
git config --global user.email "email@cua.ban"
```

### 4.3 Kiểm tra

```sh
git branch --show-current     # phải in: tong_hop_code
ls                            # phải thấy: Makefile  include  input  output  src  tests  docs ...
```

> **Không** clone vào thư mục chia sẻ với Windows (`/mnt/hgfs/...`). Hãy để code trong thư mục home của Ubuntu (`~`), nếu không sẽ gặp lỗi quyền file và lỗi xuống dòng.

---

## 5. Build

```sh
cd ~/SIMPLE_OS_CO2017
make clean
make all
```

- Lần đầu mất khoảng 1–3 phút (phải biên dịch model DMA của Verilator).
- Có vài dòng `warning: unused parameter` từ `src/mem.c` là **bình thường** (code cũ của thầy).
- Build thành công khi dòng cuối là lệnh `g++ ... -o os` và không có chữ `Error`.

Kiểm tra:

```sh
ls -l os          # phải có file os
```

Nếu `make all` lỗi, xem [mục 10](#10-xử-lý-lỗi-thường-gặp). Trong lúc chờ sửa, vẫn có thể test mọi phần trừ DMA bằng bản không Verilator:

```sh
make os-mm        # tạo ./os-mm, không cần Verilator
```

---

## 6. Chạy test của thầy

### 6.1 Chạy một test

```sh
./os sched_0
```

- `sched_0` là tên file trong thư mục `input/`, gõ không có `input/` phía trước.
- Danh sách test: `ls input`.
- Muốn lưu kết quả ra file: `./os sched_0 > ketqua.txt`. Xem file: `less ketqua.txt` (thoát bằng phím `q`).

### 6.2 Chạy tất cả test một lần (khuyên dùng)

```sh
bash tests/run_teacher_tests.sh
```

Kết quả mong đợi (số lần SWAP có thể chênh lệch giữa các lần chạy):

```text
CONFIG                       RC   PROCESS   FINISHED  ERRORS  SWAPS  DMA_DONE
os_0_mlq_paging              0    5         5         8       0      0
os_1_mlq_paging              0    8         8         5       0      0
os_1_mlq_paging_small_1K     0    8         8         23      0      0
os_1_mlq_paging_small_4K     0    8         8         5       16     0
os_1_singleCPU_mlq           0    8         8         1       0      0
os_1_singleCPU_mlq_paging    0    8         8         1       0      0
os_2_mlq_paging              0    6         6         6       0      2
os_2_singleCPU_mlq_paging    0    5         5         6       0      2
os_dma_demo                  0    2         2         0       0      6
os_mm_usr_demo               0    2         2         0       0      0
os_sc                        0    1         1         0       0      0
os_swap_demo                 0    2         2         0       21     0
os_syscall                   0    1         1         5       0      0
os_syscall_list              0    1         1         0       0      0
sched                        0    4         4         0       0      0
sched_0                      0    4         4         0       0      0
sched_1                      0    5         5         0       0      0
```

Bên dưới bảng, script liệt kê từng lỗi lệnh kèm **lệnh gốc** trong file chương trình. Log đầy đủ của mỗi test nằm ở `tests/results/<tên test>.log`.

Chỉ chạy vài test:

```sh
bash tests/run_teacher_tests.sh sched sched_0 os_dma_demo
```

### 6.3 Thế nào là đạt?

Một test **đạt** khi thoả cả ba điều:

1. `RC = 0`: chương trình không crash, không treo.
2. `FINISHED = PROCESS`: mọi process đều chạy xong.
3. `ERRORS` khớp bảng dưới. Một số test **phải** có lỗi vì chính chương trình input của thầy sai.

| Test | Số lỗi đúng | Vì sao |
|---|---|---|
| `sched`, `sched_0`, `sched_1` | 0 | Chỉ có lệnh `calc` |
| `os_1_singleCPU_mlq`, `os_1_singleCPU_mlq_paging` | 1 | `m0s`: `write 1 2 1000` ghi offset 1000 vào vùng chỉ 100 byte |
| `os_0_mlq_paging` | 8 | 2 process `p0s`, mỗi process 4 lỗi: đọc/ghi region 2, 3 chưa được `alloc` |
| `os_1_mlq_paging`, `os_1_mlq_paging_small_4K` | 5 | 4 lỗi của `p0s` + 1 lỗi của `m0s` |
| `os_1_mlq_paging_small_1K` | 23 | RAM 2048 B nhỏ hơn 1 trang 4096 B nên mọi `alloc` thất bại |
| `os_2_mlq_paging`, `os_2_singleCPU_mlq_paging` | 6 | 2 process `m2s`, mỗi process 3 lỗi: copy vào region chưa `kmalloc`, dùng region 3 chưa cấp |
| `os_syscall` | 5 | RAM 2048 B (alloc và write thất bại) + gọi syscall 101 không tồn tại |
| `os_sc`, `os_syscall_list` | 0 | |
| `os_mm_usr_demo`, `os_dma_demo`, `os_swap_demo` | 0 | Test của nhóm, luôn phải 0 lỗi |

> Không cần khớp từng dòng với thư mục `output/` của thầy, vì các CPU chạy song song và bản của nhóm in thêm nhiều log. Muốn so thử: `diff tests/results/sched_0.log output/sched_0.output`.

### 6.4 Ý nghĩa các dòng trong log

| Dòng bắt đầu bằng | Ý nghĩa |
|---|---|
| `Time slot N` | Sang time slot thứ N |
| `CPU x: Dispatched process y` / `Put process y` / `Processed y has finished` | Lập lịch |
| `[MM-CONFIG]` | Kích thước RAM, số frame dùng được, địa chỉ đầu kernel |
| `[ALLOC]`, `[KMALLOC]` | Địa chỉ ảo được cấp |
| `[VMA]` | Vùng nhớ, các region đang dùng, các vùng trống |
| `[READ]` | Giá trị đọc được |
| `[PAGE TABLE]` | Bảng trang của process khi nó kết thúc |
| `[MM-STATS]` | Thống kê: số bảng mỗi cấp, dung lượng bảng trang, số lần truy cập, số lần swap |
| `[SWAP-OUT]`, `[SWAP-IN]` | Đẩy trang ra SWAP / đưa trang về RAM |
| `[DMA-SUBMIT]`, `[DMA-DONE]` | Gửi yêu cầu DMA / DMA làm xong |
| `[INSTRUCTION-ERROR]` | Một lệnh thất bại (process vẫn chạy tiếp) |

---

## 7. Chạy test riêng của nhóm

```sh
make test-mm      # 12 nhóm test bộ nhớ, swap, syscall, CPU
make test-dma     # testbench chip DMA trên Verilator
```

Kết quả mong đợi:

- `make test-mm`: 12 dòng `PASS 01` … `PASS 12`, dòng cuối là `ALL 12 TEST GROUPS PASSED`.
- `make test-dma`: 17 dòng đều bắt đầu bằng `PASS` (ví dụ `PASS src=4096 dst=8192 size=512 result=copied`). Các dòng `result=rejected` cũng là PASS, vì đó là trường hợp cố tình sai mà chip phải từ chối.

Ba demo của nhóm và giá trị đúng:

```sh
./os os_mm_usr_demo | grep READ   # mỗi PID: 165, 254, 0
./os os_dma_demo    | grep READ   # mỗi PID: 11, 44, 77, 88, 33
./os os_swap_demo   | grep READ   # mỗi PID: 65, 66, 67
```

---

## 8. Lấy số liệu cho báo cáo

| Cần | Lệnh |
|---|---|
| Gantt chart | `./os sched > s.log && sh tests/gantt.sh s.log` (in CPU, process, khoảng time slot) |
| Trạng thái cấp phát bộ nhớ | `./os os_mm_usr_demo \| grep -E "ALLOC\|VMA\|reg=\|free="` |
| Bảng trang, thống kê | `./os os_mm_usr_demo \| grep -A4 "PAGE TABLE"` và `grep MM-STATS` |
| Swap | `./os os_swap_demo \| grep SWAP` |
| DMA bất đồng bộ | `./os os_dma_demo \| grep -E "DMA\|READ"` |
| Chi tiết bus DMA và waveform | `DMA_TRACE=1 ./os os_dma_demo`, tạo thêm file `rtl_trace.vcd` |

Giải thích ý nghĩa các con số nằm ở `docs/implementation_VI.md` (mục 4.7 và mục 12).

**Đưa file log từ máy ảo sang Windows:** cách đơn giản nhất là kéo thả file từ cửa sổ máy ảo ra Desktop Windows (cần đã làm mục 2.4). Cũng có thể dùng **VM → Settings → Options → Shared Folders** để chia sẻ một thư mục, rồi chép log vào `/mnt/hgfs/<tên thư mục>`. Chỉ chép log vào đó, **không build** trong đó.

---

## 9. Làm việc hằng ngày

### 9.1 Lấy code mới nhất rồi test

```sh
cd ~/SIMPLE_OS_CO2017
git pull
make all
bash tests/run_teacher_tests.sh
```

### 9.2 Sửa code và đẩy lên GitHub

Có thể sửa bằng VS Code trong máy ảo (cài bằng `sudo snap install code --classic`, mở bằng `code .`) hoặc bằng `nano <tên file>`.

Quy tắc của nhóm:

1. **Không commit thẳng lên `tong_hop_code` hay `main`.** Tạo nhánh riêng:
   ```sh
   git switch -c ten-nhanh-cua-ban
   ```
2. Sửa xong thì build và chạy test (mục 5–7). Số lỗi của từng test phải vẫn khớp bảng ở mục 6.3.
3. Commit và push:
   ```sh
   git add <các file đã sửa>
   git commit -m "module: mo ta ngan viec da lam"
   git push -u origin ten-nhanh-cua-ban
   ```
4. Lên GitHub mở **Pull Request** vào `tong_hop_code` để cả nhóm xem trước khi merge.
5. Không commit các file sinh ra khi build: `obj/`, `os`, `os-mm`, `test-mm-bin`, `dma_testbench`, `*.vcd`, `src/syscalltbl.lst`, `tests/results/`.
6. Không đưa tên công cụ AI vào commit (quy tắc trong `CLAUDE.md`).

---

## 10. Xử lý lỗi thường gặp

| Hiện tượng | Nguyên nhân | Cách sửa |
|---|---|---|
| VMware báo *"Intel VT-x is disabled"* | Chưa bật ảo hoá trong BIOS | Vào BIOS (thường nhấn F2/Del/F10 khi khởi động), bật **Intel Virtualization Technology** hoặc **SVM Mode** |
| Máy ảo rất chậm | Thiếu RAM/CPU | Tắt máy ảo → **Edit virtual machine settings** → tăng Memory và Processors |
| `sudo: command not found`, hoặc user không được dùng sudo | Không phải user quản trị | Đăng nhập bằng user tạo lúc cài Ubuntu |
| `Unable to locate package verilator` | Chưa `apt update` | `sudo apt update` rồi cài lại |
| `fatal: Authentication failed` khi clone | Nhập mật khẩu GitHub thay vì token, hoặc chưa được mời vào repo | Dùng token (mục 4.2); nhờ chủ repo mời vào **Settings → Collaborators** |
| `fatal error: verilated.h: No such file or directory` | Verilator chưa cài, hoặc cài ở `/usr/local` | `sudo apt install verilator`; nếu cài từ mã nguồn thì `make all VERILATOR_ROOT=/usr/local/share/verilator` |
| Lỗi biên dịch trong `Vdma_top*.cpp`, `verilated*.cpp` hoặc `dpi_rtl.cpp` | Verilator quá cũ hoặc khác phiên bản | Cài Verilator 5.032 ([Phụ lục A](#phụ-lục-a-cài-verilator-5032-từ-mã-nguồn)) rồi `make clean && make all` |
| `make: *** No rule to make target` / `$'\r': command not found` | Code bị đổi xuống dòng sang kiểu Windows (CRLF), thường do build trong `/mnt/hgfs` hoặc chép code từ Windows sang | Clone lại bằng git vào `~` (mục 4) |
| `Permission denied` khi chạy script | Script chưa có quyền chạy | Chạy bằng `bash tests/run_teacher_tests.sh`, không gõ `./tests/...` |
| `Usage: os [path to configure file]` | Gõ thiếu tên test | `./os sched_0` |
| `Invalid config: input/...` | Gõ sai tên test, hoặc file config sai định dạng | Kiểm tra `ls input`; định dạng file config xem `spec.md` mục 2.1 |
| `Cannot start DMA on simulated RAM` | Không khởi động được model DMA | Build lại `make clean && make all`; nếu vẫn lỗi thì dùng tạm `make os-mm` và `BIN=./os-mm bash tests/run_teacher_tests.sh` |
| Một test chạy mãi không dừng | Lỗi treo (không nên xảy ra) | Nhấn **Ctrl + C**; script tự dừng mỗi test sau 120 giây. Gửi log `tests/results/<test>.log` cho nhóm |
| Số lỗi lệnh khác bảng mục 6.3 | Code vừa bị sửa làm thay đổi hành vi | Xem phần chi tiết lỗi in dưới bảng, so với `git diff`; hỏi nhóm trước khi merge |

Không tự xử lý được: chụp màn hình hoặc copy **toàn bộ** thông báo lỗi, ghi rõ đang làm bước nào, rồi gửi vào nhóm.

---

## 11. Checklist trước khi demo

- [ ] `git pull` để có code mới nhất, đang ở nhánh `tong_hop_code`
- [ ] `make clean && make all` thành công
- [ ] `make test-mm` in `ALL 12 TEST GROUPS PASSED`
- [ ] `make test-dma` có 17 dòng PASS
- [ ] `bash tests/run_teacher_tests.sh`: mọi dòng `RC = 0`, `FINISHED = PROCESS`, `ERRORS` khớp mục 6.3
- [ ] Ba demo `os_mm_usr_demo`, `os_dma_demo`, `os_swap_demo` đọc ra đúng giá trị (mục 7)
- [ ] Mỗi người nắm phần mình trình bày: scheduler (mục 5), bộ nhớ và swap (mục 4), DMA (mục 7) trong `docs/implementation_VI.md`

---

## Phụ lục A. Cài Verilator 5.032 từ mã nguồn

Chỉ làm khi `verilator --version` không ra 5.032 **và** `make all` báo lỗi ở phần DMA. Mất khoảng 15–30 phút.

```sh
sudo apt update
sudo apt install -y git help2man perl python3 make autoconf g++ flex bison ccache \
                    libfl2 libfl-dev zlib1g zlib1g-dev
cd ~
git clone https://github.com/verilator/verilator.git
cd verilator
git checkout v5.032
autoconf
./configure
make -j"$(nproc)"
sudo make install
verilator --version          # phải in Verilator 5.032
```

Bản cài từ mã nguồn nằm ở `/usr/local/share/verilator`, nên khi build project phải chỉ rõ đường dẫn:

```sh
cd ~/SIMPLE_OS_CO2017
make clean
make all VERILATOR_ROOT=/usr/local/share/verilator
make test-dma VERILATOR_ROOT=/usr/local/share/verilator
```

Nếu máy ảo vẫn còn Verilator cũ do `apt` cài, gỡ nó đi để tránh nhầm: `sudo apt remove verilator`.

---

## Phụ lục B. Tóm tắt lệnh

```sh
# Một lần
sudo apt update && sudo apt install -y build-essential git verilator
git clone -b tong_hop_code https://github.com/d4sync/SIMPLE_OS_CO2017.git ~/SIMPLE_OS_CO2017

# Mỗi lần test
cd ~/SIMPLE_OS_CO2017
git pull
make clean && make all
bash tests/run_teacher_tests.sh
make test-mm
make test-dma

# Một test lẻ / Gantt
./os sched_0
sh tests/gantt.sh tests/results/sched.log
```
