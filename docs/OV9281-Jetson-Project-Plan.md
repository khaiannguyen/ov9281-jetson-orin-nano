# Dự án: Port Driver OV9281 (R35.6.5 → R39.2) + Device Tree cho 2x OV9281-120 trên Jetson Orin Nano (cam0/cam1)

## 🎯 THAY ĐỔI CHIẾN LƯỢC LỚN (cập nhật quan trọng nhất)

**Đã tìm thấy driver gốc chính thức của NVIDIA cho OV9281**, trong source R35.6.5:
```
~/Downloads/r35.6.5_source/Linux_for_Tegra/source/public/kernel/nvidia/drivers/media/i2c/nv_ov9281.c
~/Downloads/r35.6.5_source/Linux_for_Tegra/source/public/kernel/nvidia/drivers/media/i2c/ov9281_mode_tbls.h
```

Bằng chứng từ tài liệu "Camera Driver Porting" (R39.2): bảng "Configuration Changes" liệt kê `CONFIG_NV_VIDEO_OV9281=m` tồn tại ở kernel 5.10 (R32/R35), nhưng cột kernel 6.8 (R38/R39) ghi "N/A" — nghĩa là NVIDIA đã **loại bỏ** OV9281 khỏi danh sách sensor build sẵn khi lên kernel 6.8, nhưng source code gốc (thời R35) vẫn còn nguyên trong package BSP Sources.

**→ Dự án đổi từ "viết driver mới dựa trên khung OV5693" thành "PORT + MIGRATE FRAMEWORK driver gốc NVIDIA từ kernel 5.10 API sang kernel 6.8 API".** Đây là câu chuyện dự án mạnh hơn nhiều để trình bày: tìm được driver chính thức đã bị deprecate, tự tay migrate qua 2 thế hệ kernel API VÀ 2 thế hệ framework (v1 → tegracam v2.0), viết lại device tree theo chuẩn hiện tại.

Toàn bộ danh sách API đã đổi (5.10 → 6.8) đã có sẵn, chi tiết, trong tài liệu "Camera Driver Porting" (R39.2) — dùng làm checklist port trực tiếp (xem Giai đoạn 1).

## 🔴 Phát hiện thứ 2 (quan trọng ngang phát hiện đầu): khác biệt KIẾN TRÚC framework, không chỉ API

Đối chiếu danh sách hàm 3 file (`grep -n "^static.*(" *.c`) cho thấy `nv_ov9281.c` (R35.6.5) viết theo **framework v1 cũ** (tự implement `v4l2_subdev_ops`/`v4l2_ctrl_ops` trực tiếp), trong khi `nv_ov5693.c` và `nv_imx219.c` (R39.2) đã chuyển sang **Jetson V4L2 Camera Framework v2.0 (tegracam)** — đúng như tài liệu "Sensor Software Driver Programming" khuyến nghị ("We recommend using only this version for new driver development").

Bảng đối chiếu cụ thể (từ kết quả `grep` thực tế):

| `nv_ov9281.c` (framework v1, cần thay thế) | `nv_ov5693.c`/`nv_imx219.c` (framework tegracam v2.0, khuôn mẫu) | Việc cần làm khi port |
|---|---|---|
| `ov9281_s_stream(struct v4l2_subdev *sd, int enable)` — 1 hàm viết tay toàn bộ logic on/off | `ov5693_start_streaming(tc_dev)` + `ov5693_stop_streaming(tc_dev)` — 2 hàm riêng, framework tự gọi | Tách `ov9281_s_stream()` gốc thành 2 hàm `ov9281_start_streaming()`/`ov9281_stop_streaming()`, đổi tham số sang `struct tegracam_device *tc_dev` |
| `ov9281_s_ctrl(struct v4l2_ctrl *ctrl)` — switch-case tự xử lý mọi control | Không có; thay bằng hàm riêng: `ov5693_set_gain()`, `ov5693_set_exposure()`, `ov5693_set_frame_rate()`, `ov5693_set_group_hold()` | Tách từng case trong `ov9281_s_ctrl()` gốc thành hàm riêng tương ứng, đăng ký qua `ctrl_cid_list[]` + `tegracam_ctrl_ops` |
| `ov9281_g_volatile_ctrl()` | Không có (framework tự xử lý read-back) | Bỏ, logic đọc lại giá trị chuyển vào từng hàm `set_*` nếu cần |
| `ov9281_set_fmt()` / `ov9281_get_fmt()` viết tay | Không có (framework xử lý qua mode table + `tegra_channel_fmt`) | Bỏ, đảm bảo mode table (`ov9281_mode_tbls.h`) đủ thông tin để framework tự suy ra |
| `ov9281_ctrls_init()` viết tay | Không có (framework tự khởi tạo từ `ctrl_cid_list[]`) | Bỏ, thay bằng khai báo `ctrl_cid_list[]` tĩnh giống OV5693 |
| Không có `set_mode()` | `ov5693_set_mode(tc_dev)` | Thêm hàm mới, framework gọi khi đổi resolution/mode |
| `ov9281_power_on/off(struct camera_common_data *s_data)` | `ov5693_power_on/off(struct camera_common_data *s_data)` — **giữ nguyên chữ ký!** | Giữ gần như nguyên, chỉ cần đối chiếu API con bên trong (GPIO/regulator) theo bảng Giai đoạn 1 |
| `ov9281_power_put/get(struct ov9281 *priv)` | `ov5693_power_put/get(struct tegracam_device *tc_dev)` | Đổi tham số sang `tc_dev`, lấy `priv` qua `tegracam_get_privdata(tc_dev)` |
| `ov9281_probe(struct i2c_client *client, const struct i2c_device_id *id)` (2 tham số — API i2c cũ) | `ov5693_probe(struct i2c_client *client)` (1 tham số — API i2c mới, khớp bảng `i2c_new_client_device()` ở Giai đoạn 1) | Đổi chữ ký hàm, và toàn bộ nội dung `probe()` viết lại theo khuôn tegracam (`tegracam_device_register()`) |
| Không có `imx219_open()`/`ov5693_open()` | Có (v4l2_subdev_open callback riêng) | Thêm hàm tương ứng theo khuôn |
| Không có `remove()` tách khỏi `probe()` | `imx219_remove()`/`ov5693_remove()` (và cả 2 chữ ký cũ/mới do `#if LINUX_VERSION_CODE` — xem ngay dòng đôi 696/698 và 760/762 trong `nv_imx219.c`) | Đây là ví dụ **trực tiếp** cách NVIDIA dùng `LINUX_VERSION_CODE` guard để hỗ trợ 2 kernel API cùng lúc — copy pattern này khi cần |

**Kết luận:** việc port không chỉ là đổi API cũ→mới bên trong từng hàm (Giai đoạn 1 cũ), mà là **viết lại toàn bộ kiến trúc control-flow của driver theo khuôn tegracam v2.0**, dùng `nv_ov5693.c` làm khung cấu trúc chính, và lấy nội dung logic (giá trị register cụ thể, cách tính gain/exposure của OV9281) từ bên trong từng hàm tương ứng của `nv_ov9281.c` gốc.

---

## Bối cảnh hệ thống (đã xác nhận)

- **Board:** NVIDIA Jetson Orin Nano Engineering Reference Developer Kit Super
- **L4T đích (chạy thật):** R39 (release), REVISION 2.0, BOARD generic — build 2026/06/01, JetPack 7.2, kernel 6.8.12-1021-tegra (variant `oot`)
- **CUDA:** 13.2 / **TensorRT:** 10.16.2 / **cuDNN:** 9.20.0
- **CSI baseline đã verify:** IMX219 chạy ổn qua `nvarguscamerasrc` (sensor-id=0, 1280x720@60fps NV12) → xác nhận CSI PHY, ISP/Argus pipeline, và toolchain flash/build cơ bản đều hoạt động đúng trên carrier board này.
- **Camera mục tiêu:** 2x Waveshare OV9281-120 (OmniVision OV9281, mono global-shutter, 1/4" 1MP, 1280x800@120fps hoặc VGA@180fps, 2-lane MIPI CSI-2, output RAW8/RAW10, I2C/SCCB).
- **Nguồn source đã tải:**
  - R39.2 (đích, chạy thật), **cây đầy đủ** (2.1GB, có cả `hardware/nvidia/t23x` chứa toàn bộ DT overlay mẫu — dùng để lấy `tegra234-p3767-camera-p3768-imx219-dual.dts` thật ở Giai đoạn 3): `~/Downloads/public_sources_39.2/Linux_for_Tegra/source`
  - R39.2, cây rút gọn cũ (chỉ `kernel_oot_modules_src.tbz2` + `kernel_src.tbz2`, KHÔNG có `hardware/`): `~/Downloads/public_sources/Linux_for_Tegra/source` — giữ lại nhưng **dùng bản `_39.2` ở trên khi cần file DT/overlay**.
  - **R35.6.5 (nguồn port, chứa driver gốc):** `~/Downloads/r35.6.5_source/Linux_for_Tegra/source/public` — đã giải nén, có `nv_ov9281.c` thật
- **Datasheet OV9281 đầy đủ:** ✅ Đã có và đã đọc trích xuất chi tiết (`OV9281-datasheet.pdf`, PRELIMINARY SPECIFICATION v1.22, 133 trang). Các thông số quan trọng đã trích ra — xem mục "📋 Dữ liệu kỹ thuật đã trích từ datasheet" bên dưới.
- **Xác nhận từ docs NVIDIA (Camera Support Matrix, R39.2):** OV9281 không nằm trong reference sensor support hiện tại (R39.2) — khớp với việc NVIDIA đã bỏ nó khỏi kernel 6.8 config. Dùng chi tiết này khi viết README cuối dự án.
- **Kiến trúc dự kiến dùng:** OV9281 là RAW mono, không tích hợp ISP kiểu Bayer → bring-up chính đi qua **V4L2 trực tiếp** (`v4l2-ctl --stream-mmap`) trước, sau đó thử `nvarguscamerasrc`/libargus. Ghi chú debug từ NVIDIA: nếu `v4l2-ctl` capture được nhưng Argus không chạy → kiểm tra `ctrl_ops`/CID trước (vd hàm tương đương `ov5693_set_exposure()`), không phải CSI/DT.

## 📋 Dữ liệu kỹ thuật đã trích từ datasheet OV9281 (dùng trực tiếp khi port code)

### I2C/SCCB address — quan trọng, xác nhận sự khác biệt

- **Chuẩn (mục 7.1):** slave address = **0xC0 (write) / 0xC1 (read)** dạng 8-bit → tương đương **7-bit address = 0x60**.
- **⚠️ Lưu ý quan trọng từ schematic (mục 2.3, MIPI reference design):** địa chỉ thực tế phụ thuộc chân **SID** (pin E1) trên module:
  - SID nối GND → SCCB address = **0xC0** (0x60 7-bit, giống mặc định)
  - SID nối DOVDD → SCCB address = **0x20** (0x10 7-bit)
  - **Cần xác định chân SID trên module Waveshare nối thế nào** (đo bằng `i2cdetect -y <bus>` sau khi có driver chạy tạm, hoặc dò schematic Waveshare nếu có). Đây là lý do nếu 2 camera OV9281 giống hệt nhau gắn cùng bus I2C sẽ **đụng địa chỉ** — cần xác nhận cam0/cam1 có tách bus I2C riêng trong DT (giống IMX219 dual) để tránh xung đột, hoặc SID phải strap khác nhau giữa 2 module.

### Chip ID — dùng cho hàm `verify_chip_id()`/`ov9281_verify_chip_id`

- `0x300A` (SC_CHIP_ID_HIGH) = **0x92** (mặc định, read-only)
- `0x300B` (SC_CHIP_ID_LOW) = **0x81** (mặc định, read-only)
- → Chip ID đầy đủ = **0x9281** — khớp tên sensor, dùng để xác nhận `probe()` đọc đúng chip trước khi tiếp tục.

### Streaming on/off — dùng cho `ov9281_start_streaming()`/`stop_streaming()`

- Register `0x0100` (SC_MODE_SELECT), bit[0]: **0 = software standby, 1 = streaming**. Đây chính là register cần ghi trong hàm start/stop streaming (thay vì đoán mò, đã có địa chỉ chính xác).
- Lưu ý ghi trong mục 2.5.2 (power down sequence): nếu lệnh SCCB "exit streaming" đến giữa lúc đang xuất 1 frame MIPI, sensor sẽ đợi tới cuối frame (MIPI end code) mới vào standby; nếu đến lúc giữa 2 frame thì vào standby ngay lập tức — cần lưu ý khi viết `stop_streaming()`, không cần thêm delay thủ công vì sensor tự xử lý.

### Power up/down sequence — dùng cho `power_on()`/`power_off()` (mục 2.5)

**Power up (thứ tự bắt buộc):**
1. AVDD và DOVDD có thể lên bất kỳ thứ tự nào, nhưng phải ổn định trước khi kéo XSHUTDOWN lên cao
2. DVDD lên sau DOVDD, nhưng trước khi XSHUTDOWN lên cao
3. XSHUTDOWN được kéo cao **sau khi** AVDD và DOVDD đã ổn định
4. Timing tối thiểu quan trọng: **t3/t4 = 8192 chu kỳ XVCLK** phải trôi qua giữa lúc XSHUTDOWN lên cao và giao dịch SCCB đầu tiên — đây là delay thường bị bỏ sót khi port, cần thêm đúng vào `power_on()`.
5. PLL lock time (t5) ≈ 0.2ms.

**Power down (thứ tự bắt buộc):**
1. Khuyến nghị vào software standby trước (ghi `0x0100=0`)
2. Kéo XSHUTDOWN xuống thấp (để tiết kiệm điện tối đa)
3. Kéo DVDD xuống thấp
4. AVDD và DOVDD có thể xuống bất kỳ thứ tự nào
5. Timing: cần ít nhất **512 chu kỳ XVCLK** sau giao dịch SCCB/MIPI cuối cùng trước khi kéo XSHUTDOWN xuống.

**Reset (mục 2.6):** chân XSHUTDOWN (pin A5) kéo xuống thấp (GND) = hardware reset toàn bộ, tất cả register về giá trị mặc định.

### PLL configuration — dùng để tính `pix_clk_hz` cho device tree (mục 2.8)

Công thức tổng quát (2 tầng PLL, `PLL1` dùng cho pixel/MIPI clock, `PLL2` dùng cho ADC/analog clock):
```
PLL1_pix_clk = XVCLK / PLL1_pre_div / PLL1_pre_div0 × PLL1_multiplier / PLL1_M_div
MIPI_clk     = PLL1_pix_clk / PLL1_MIPI_div
SYS_CLK      = PLL1_pix_clk / PLL1_sys_pre_div / PLL1_sys_div
```

**Bảng cấu hình mẫu chính thức từ datasheet (mục 2.8.1, table 2-10) — dùng làm điểm khởi đầu cho mode 1280x800@120fps, 2-lane, 10-bit RAW:**

| Thông số | Giá trị |
|---|---|
| EXTCLK (XVCLK input) | 24 MHz |
| SYS_CLK | 80 MHz |
| MIPI_PCLK | 100 MHz |
| **MIPI_CLK (data rate/lane)** | **800 Mbps** |

→ Đây chính là bộ số cần đối chiếu khi tính `pix_clk_hz`/`mclk_multiplier` trong `.dtsi` (Giai đoạn 2-3) — không cần tự suy luận từ số 0, datasheet đã cho sẵn bộ giá trị PLL mẫu chính thức cho đúng mode 120fps mà bạn định dùng.

### Format/resolution hỗ trợ chính thức (mục 2.3, table 2-1)

| Format | Resolution | Max frame rate | Phương pháp | MIPI data rate |
|---|---|---|---|---|
| Full resolution | 1280x800 | 120 fps | full | 2-lane @ 800Mbps |
| 720p | 1280x720 | 130 fps | cropping | 2-lane @ 800Mbps |
| VGA | 640x480 | 180 fps | cropping | 2-lane @ 800Mbps |
| — | 640x400 | 210 fps | 4:1 sub-sampling | 2-lane @ 800Mbps |

Tất cả mode đều dùng **2-lane MIPI, 800Mbps/lane** — xác nhận `num_lanes = 2` cố định cho mọi mode trong DT, không cần lo lane count đổi theo resolution.

### SCCB message format (mục 2.9.2)

16-bit sub-address + 8-bit data + 7-bit slave address — chuẩn I2C thường, không có gì đặc biệt khác biệt so với IMX219/OV5693 ở tầng giao thức (khác biệt chỉ ở giá trị register cụ thể).

---

## Nguyên tắc chỉ đạo toàn dự án

1. **Nguồn nội dung/logic:** lấy từ `nv_ov9281.c` + `ov9281_mode_tbls.h` (R35.6.5) — register sequence, giá trị gain/exposure formula, GPIO/power sequence cụ thể cho OV9281.
2. **Nguồn kiến trúc/khung sườn:** copy cấu trúc từ `nv_ov5693.c` (R39.2, cùng hãng OmniVision, đã ở framework tegracam v2.0) — đây là khuôn chính cho toàn bộ file mới `nv_ov9281.c` (bản port). Đối chiếu bảng "Phát hiện thứ 2" ở trên cho từng cặp hàm.
3. **IMX219** (đã chạy ổn trên board thật) dùng làm baseline đối chiếu khi debug CSI/DT — nếu IMX219 chạy nhưng OV9281 không, lỗi nằm ở driver/DT bạn port, không phải phần cứng.
4. **Datasheet OV9281 đầy đủ** dùng để xác nhận lại từng bước port có đúng logic phần cứng không (không chỉ sửa API suông mà không hiểu ý nghĩa).
5. Trung thực khi viết README: đây là dự án **port + migrate framework** driver NVIDIA gốc đã deprecate (không chỉ đổi API kernel mà còn nâng cấp từ framework v1 lên tegracam v2.0), không phải "viết từ số 0" — nói rõ vẫn đúng giá trị kỹ thuật, không cần overstate.

---

## Giai đoạn 0 — Đối chiếu 3 bản driver ✅ ĐÃ XONG (kết quả `grep -n "^static.*("` cho cả 3 file)

- [x] Đối chiếu danh sách hàm `nv_ov9281.c` (R35.6.5) vs `nv_ov5693.c` + `nv_imx219.c` (R39.2).
- [x] **Phát hiện kiến trúc:** `nv_ov9281.c` dùng framework v1 (viết tay `s_stream`/`s_ctrl`/`set_fmt`/`ctrls_init`), còn R39.2 dùng tegracam v2.0 (`start_streaming`/`stop_streaming`/`set_mode`, control tách hàm riêng). Chi tiết đầy đủ ở bảng "Phát hiện thứ 2" phần đầu file.
- [ ] Việc tiếp theo: đọc chi tiết nội dung từng cặp hàm tương ứng (không chỉ tên), bắt đầu từ cặp dễ nhất trước.

**Thứ tự đọc cặp hàm đề xuất (dễ → khó, để làm quen dần với việc "mổ" hàm cũ thành hàm mới):**
1. `ov9281_power_on/off(s_data)` ↔ `ov5693_power_on/off(s_data)` — chữ ký giữ nguyên, chỉ khác nội dung GPIO/regulator cụ thể → cặp dễ nhất, làm trước để quen nhịp đọc.
2. `ov9281_read_reg/write_reg/write_table` ↔ `ov5693_read_reg/write_reg/write_table` — I/O cơ bản, khả năng gần như giữ nguyên cấu trúc.
3. `ov9281_set_gain()` (nằm trong `ov9281_s_ctrl` case, cần tách ra) ↔ `ov5693_set_gain(tc_dev, val)` — ví dụ đầu tiên của việc "mổ" switch-case cũ thành hàm riêng.
4. `ov9281_s_stream()` (viết tay) ↔ `ov5693_start_streaming()`/`ov5693_stop_streaming()` — cặp quan trọng nhất, quyết định camera có stream được không.
5. `ov9281_probe()` ↔ `ov5693_probe()` — làm cuối cùng vì đây là hàm tổng hợp, gọi tất cả hàm trên; đọc dễ hơn nhiều sau khi đã hiểu từng phần.

**Kết quả `find` source đã xác nhận trước đó (giải nén `kernel_oot_modules_src.tbz2` + `kernel_src.tbz2` vào `~/Downloads/public_sources/Linux_for_Tegra/source`):**

## Giai đoạn 1 — Áp checklist "Camera Driver Porting" (R39.2) vào từng phần code

Danh sách thay đổi API đã xác nhận từ tài liệu chính thức (dùng làm checklist khi sửa `nv_ov9281.c`):

| Vùng thay đổi | Kernel 5.10 (bản gốc) | Kernel 6.8 (cần sửa thành) | File tham khảo path (R39.2) |
|---|---|---|---|
| Power Domain framework | API cũ | Header mới | `/kernel/kernel-noble/include/linux/pm_domain.h` |
| Timestamp / V4L2 buffer state | `ktime_get_ts64`, `struct timespec64`, `VB2_BUF_STATE_ERROR` kiểu cũ | API mới tương ứng | `/kernel/kernel-noble/include/media/v4l2-dev.h` |
| V4L2 async struct | Cấu trúc cũ | Cấu trúc mới | `/kernel/kernel-noble/include/media/v4l2-async.h` |
| C-PHY/D-PHY enum trong `v4l2_mbus_config` | enum cũ | enum mới | `/kernel/kernel-noble/include/uapi/linux/v4l2-mediabus.h` |
| debugfs API | API cũ | API mới | `/kernel/kernel-noble/fs/debugfs/file.c` |
| kmap → vmap dma_buf | `kmap` | `vmap` dma_buf API | `/kernel/kernel-noble/drivers/dma-buf/dma-buf.c` |
| `dev_err()` | Định nghĩa cũ | Định nghĩa mới | `/kernel/kernel-noble/include/linux/dev_printk.h` |
| I2C API | API cũ | `i2c_lock_bus()`/`i2c_unlock_bus()`, `i2c_new_client_device()` | `/kernel/kernel-noble/include/linux/i2c.h`, `/kernel/kernel-noble/drivers/i2c/i2c-core-base.c` |
| NVIDIA capture driver path (VI/ISP channel config) | Path cũ | Path mới | `/nvidia-oot/drivers/media/platform/tegra/camera/fusa-capture/` |

- [ ] Với mỗi dòng trên, `grep` trong `nv_ov9281.c` gốc xem có dùng API/struct cũ đó không.
- [ ] Đối chiếu cách `nv_ov5693.c` (bản đã port, R39.2) xử lý đúng phần tương ứng — copy pattern, áp dụng vào `nv_ov9281.c`.
- [ ] Build thử sau mỗi nhóm thay đổi nhỏ (không sửa hết một lần rồi mới build — dễ lạc lỗi).

## Giai đoạn 2 — Kiểm tra mode table & register logic với datasheet thật ✅ Phần lớn dữ liệu đã có sẵn (xem mục "📋 Dữ liệu kỹ thuật" đầu file)

- [ ] Đối chiếu `ov9281_mode_tbls.h` (gốc, R35.6.5) với: chip ID (0x300A/0x300B = 0x9281), streaming register (0x0100), PLL sample config (table 2-10) — xác nhận NVIDIA gốc dùng đúng các giá trị này không, hay có sai khác (bản preliminary spec có thể cập nhật khác bản NVIDIA dùng lúc viết driver 2016-2017).
- [ ] Đối chiếu `probe()`/power-up sequence trong `nv_ov9281.c` với trình tự đã trích ở trên — đặc biệt kiểm tra delay **8192 chu kỳ XVCLK** (t3/t4) có được implement đúng không, đây là delay hay bị bỏ sót.
- [ ] Tính `pix_clk_hz` cho DT dựa trên bảng PLL mẫu (EXTCLK 24MHz → SYS_CLK 80MHz → MIPI_PCLK 100MHz → MIPI_CLK 800Mbps/lane, 2-lane).
- [ ] Xác nhận format target: RAW10 (datasheet ghi rõ 10-bit ADC output, "8/10-bit MIPI images" — ưu tiên dùng RAW10 để giữ đủ dữ liệu, có thể fallback RAW8 nếu cần băng thông thấp hơn).
- [ ] **Xác định chân SID trên module Waveshare** (GND→0xC0/0x60 7-bit, hoặc DOVDD→0x20/0x10 7-bit) — quan trọng cho việc gán I2C address đúng trong DT, và để tránh xung đột địa chỉ khi 2 camera cùng loại gắn khác bus.

## Giai đoạn 3 — Viết/Port Device Tree Overlay — ✅ ĐÃ XONG (2026-08-28)

- [x] ✅ Đã xác nhận: không có overlay `.dtsi` gốc cho OV9281 trong R35.6.5 (`grep -rl "ov9281"` và `grep -rl "ovti"` trong toàn bộ `./hardware/nvidia` đều rỗng). NVIDIA chỉ cung cấp driver `.c`+mode table, không kèm DT mẫu cho sensor này.
- [x] **Phương án đã dùng:** khung `tegra234-p3767-camera-p3768-imx219-dual.dts` + `tegra234-camera-rbpcv2-imx219.dtsi` (R39.2, đọc file THẬT từ `public_sources_39.2`, không suy đoán) làm nền. **Không dùng `ovti,ov5693.yaml`** — đã đọc kỹ, đó là binding chuẩn upstream/mainline (kiểu OF-graph `clocks=`/`reset-gpios` phẳng), KHÁC hẳn kiểu property NVIDIA legacy thật sự dùng trong `.dts` (`avdd-reg`/`mclk_khz`/`mode0{...}` bên trong node) — driver `.c` đã port (`ov9281_parse_dt`) đọc đúng kiểu property NVIDIA legacy, nên DT phải theo kiểu đó, không theo yaml mainline.
- [x] Sửa `compatible = "ovti,ov9281"`.
- [x] Sửa mode-timing: `active_w/active_h` theo cả 3 mode (1280x800, 1280x720, 640x400), `pixel_t="RAW10"` (placeholder gần nhất — `sensor_common.c` không có entry mono/Y10 thật, không ảnh hưởng mbus code thật), `pix_clk_hz=80000000` tính từ Giai đoạn 2.
- [x] GPIO reset (XSHUTDOWN): dùng lại 2 chân GPIO của IMX219 (CAM0_PWDN=`TEGRA234_MAIN_GPIO(H,6)`, CAM1_PWDN=`TEGRA234_MAIN_GPIO(AC,0)`) — ✅ **ĐÃ VERIFY THẬT** (2026-08-28) bằng cách dump device tree đang chạy thật (`sudo dtc -I fs /proc/device-tree -O dts`) và giải mã property `reset-gpios` thật trên node IMX219 đang chạy, khớp chính xác 2 giá trị trên. Không còn là suy luận từ file nguồn tĩnh — mục treo đã đóng (xem bảng cuối file).
- [x] Giữ nguyên cấu trúc `nvcsi`/`vi` port mapping cam0/cam1 (không phụ thuộc sensor).
- [x] **Phát hiện thêm ngoài checklist gốc** (đọc code `.c` thật của IMX219/OV5693 để đối chiếu, không phải suy đoán):
  - Không khai `"mclk"` property — board P3768 không có clock `cam_mclk1`/`cam_mclk2` nào trong cây BPMP/T234; MCLK là oscillator cố định luôn bật. Đã sửa kèm `ov9281_power_get()` trong `.c` (thêm guard `if (pdata->mclk_name)`) để khớp — bản trước có bug thật sẽ làm `probe()` fail ở Giai đoạn 4.
  - Không khai `avdd-reg`/`dvdd-reg`/`iovdd-reg` — board P3768 không có regulator riêng cho khe 22-pin (nguồn cố định luôn bật), IMX219 thật cũng không khai.
  - Cơ chế 2 bus I2C thật sự là **i2c-mux-gpio** (1 controller vật lý + GPIO mux), không phải "2 bus vật lý riêng" như ghi trước đó — đã sửa lại mục "Quyết định kiến trúc" trong PROGRESS-SUMMARY.md.
  - Đã compile-test 2 file `.dtsi`/`.dts` bằng `cpp` + `dtc -@` thật với header thật từ `public_sources_39.2` → 0 lỗi, warning giống hệt bản IMX219 gốc của NVIDIA (không phải lỗi mới).
- [x] File kết quả: `~/Downloads/tegra234-camera-ov9281-dual.dtsi`, `~/Downloads/tegra234-p3767-camera-p3768-ov9281-dual.dts`.

## Giai đoạn 4 — Build & Flash (R39.2, quy trình đã xác nhận)

```bash
export CROSS_COMPILE=<toolchain-path>/aarch64-none-linux-gnu/bin/aarch64-none-linux-g...
export KERNEL_HEADERS=$PWD/kernel/kernel-noble
make dtbs
cp kernel-devicetree/generic-dts/dtbs/* <install-path>/Linux_for_Tegra/kernel/dtb/
```
- [ ] Build `nv_ov9281.c` đã port như out-of-tree module (theo cách `nvidia-oot` build ra `.ko`, giống `nv_ov5693.ko`).
- [ ] Deploy DTB + module lên board, reboot.

## Giai đoạn 5 — Bring-up từng bước

- [ ] `dmesg | grep -i ov9281` — xác nhận probe thành công.
- [ ] `v4l2-ctl --list-devices` — xác nhận `/dev/videoX` cho cả 2 sensor.
- [ ] `v4l2-ctl -d /dev/videoX --list-formats-ext` — xác nhận format/resolution.
- [ ] Capture raw frame trực tiếp:
  ```bash
  v4l2-ctl --set-fmt-video=width=1280,height=800,pixelformat=<theo Giai đoạn 2> \
    --stream-mmap --stream-count=1 -d /dev/video0 --stream-to=ov9281_cam0.raw
  ```
- [ ] Verify raw file bằng Python/numpy (đúng bit depth, reshape resolution) trước khi qua ISP.
- [ ] Thử `nvarguscamerasrc sensor-id=0` (đổi resolution/format so với lệnh IMX219 gốc).
- [ ] Nếu `v4l2-ctl` OK nhưng Argus không chạy → kiểm tra `ctrl_ops`/CID trước tiên (theo ghi chú NVIDIA).

## Giai đoạn 6 — Cả 2 camera đồng thời (cam0 + cam1)

- [ ] Test riêng từng cam ổn định trước.
- [ ] Test đồng thời — theo dõi I2C bus conflict / GPIO share nếu DT khai sai.

## Giai đoạn 7 — Đóng gói repo cho nhà tuyển dụng

- [ ] README: kể đúng câu chuyện — tìm thấy driver NVIDIA gốc đã deprecate (R35, kernel 5.10) → port qua kernel 6.8 API (R39.2) theo checklist chính thức NVIDIA → viết/sửa device tree → bring-up 2 camera đồng thời.
- [ ] Trích dẫn cụ thể: bảng `CONFIG_NV_VIDEO_OV9281=m → N/A` trong "Camera Driver Porting" làm bằng chứng driver từng tồn tại rồi bị bỏ.
- [ ] Trích dẫn Camera Support Matrix (R39.2) — xác nhận OV9281 không còn trong reference support hiện tại.
- [ ] Liệt kê cụ thể từng API đã port (bảng Giai đoạn 1) kèm diff trước/sau.
- [ ] Kèm ảnh/video/raw capture thật + log `dmesg`/`v4l2-ctl` làm bằng chứng.
- [ ] Ghi lại lỗi thực tế gặp phải khi debug — phần thể hiện kỹ năng rõ nhất.

---

## Danh sách thông tin còn thiếu (cần bạn cung cấp để đi tiếp)

| # | Thông tin cần | Ưu tiên | Ghi chú |
|---|---|---|---|
| 1 | ~~Kết quả `find`/`grep` overlay DT gốc OV9281~~ | ✅ Đã xong — **không tồn tại**, xác nhận qua `grep -rl "ov9281"` và `grep -rl "ovti"` trong toàn bộ `./hardware/nvidia` (R35.6.5), cả 2 đều rỗng | Chốt phương án: dùng khung `tegra234-p3767-camera-p3768-imx219-dual.dts` (R39.2) làm nền cho Giai đoạn 3, áp property OmniVision/SCCB theo `ovti,ov5693.yaml` |
| 2 | Nội dung/cấu trúc `nv_ov9281.c` (gốc, R35.6.5) — đọc cùng nhau, theo thứ tự cặp hàm đã đề xuất ở Giai đoạn 0 (power_on/off trước, rồi read/write_reg, set_gain, s_stream, cuối cùng probe) | 🔴 Bắt buộc, đang làm — bước tiếp theo | Đúng tinh thần "tự tay hiểu" đã thống nhất |
| 3 | Nội dung `ov9281_mode_tbls.h` (gốc) — đối chiếu với dữ liệu đã trích (chip ID, PLL sample config, register 0x0100) | 🟡 Nên làm cùng lúc với #2 | |
| 4 | Nội dung `nv_ov5693.c` (R39.2, bản đã lên kernel 6.8/tegracam v2.0) — dùng làm mẫu "post-port" để so sánh | 🟡 Nên có sớm, chặn Giai đoạn 1 | |
| 5 | I2C address 7-bit thực tế phụ thuộc chân SID (0x60 nếu SID→GND, 0x10 nếu SID→DOVDD) trên module Waveshare — cần xác nhận bằng đo thực tế | 🟡 Cần trước Giai đoạn 3 | Dùng `i2cdetect -y <bus>` sau khi có driver chạy tạm, hoặc hỏi Waveshare/xem schematic nếu có |
| 6 | ~~RAW8 hay RAW10~~ | ✅ Đã xác định — ưu tiên RAW10 (xem mục "📋 Dữ liệu kỹ thuật" đầu file) | |
| 7 | ~~Sơ đồ GPIO reset (XSHUTDOWN) thực tế nối vào chân nào trên carrier board~~ | ✅ Đã xong (2026-08-28) — verify bằng device tree ĐANG CHẠY THẬT (`/proc/device-tree`): `TEGRA234_MAIN_GPIO(H,6)` (cam0), `TEGRA234_MAIN_GPIO(AC,0)` (cam1) | Xem `tegra234-p3767-camera-p3768-ov9281-dual.dts` |
