# Hướng dẫn chi tiết: Sửa/Port `nv_ov9281.c` (R35.6.5 → R39.2, framework v1 → tegracam v2.0)

> File này là **hướng dẫn thao tác cụ thể**, đi kèm file `OV9281-Jetson-Project-Plan.md` (giữ nguyên, không đổi). Dùng file này khi ngồi vào sửa code thật — mỗi mục tương ứng 1 hàm, có: vị trí trong file gốc, việc cần làm, khuôn mẫu tham khảo từ `nv_ov5693.c`, và dữ liệu datasheet liên quan.

## 📍 Trạng thái tiến độ đọc code (cập nhật liên tục)

| Hàm | Đã đọc code thật? | Kết luận |
|---|---|---|
| `read_reg`/`write_reg`/`write_table` (Phần 1.1) | ✅ | Đã port xong, chỉ cần đổi `priv->regmap`→`s_data->regmap` |
| `ov9281_i2c_addr_assign` (Phần 1.2) | ✅ | Giữ nguyên hàm, không phụ thuộc để chạy được (dùng 2-bus-riêng) |
| `power_on`/`power_off` (Phần 2.1) | ✅ | XSHUTDOWN xử lý ở DT (gpio-regulator), không có trong `.c` |
| `power_put`/`power_get` (2.2) | ⬜ Chưa đọc | |
| `set_group_hold` (2.3) | ⬜ Chưa đọc | |
| `set_gain` (2.4) | ⬜ Chưa đọc | |
| `set_frame_length`/`set_coarse_time` (2.5) | ⬜ Chưa đọc | |
| `s_stream` (3.1) | ⬜ Chưa đọc — quan trọng nhất còn lại | |
| `g_input_status` (3.2) | ⬜ Chưa đọc | |
| `set_fmt`/`get_fmt` (3.3) | ✅ | Wrapper rỗng, xóa an toàn |
| `g_volatile_ctrl` (4.1) | ⬜ Chưa đọc | |
| `s_ctrl` (4.2) | ⬜ Chưa đọc | |
| `ctrls_init` (4.3) | ⬜ Chưa đọc | |
| `parse_dt` (5.1) | ⬜ Chưa đọc | |
| `verify_chip_id` (5.2) | ⬜ Chưa đọc | |
| `probe` (5.3) | ⬜ Chưa đọc — cần tìm chỗ gán `OV9281_DEFAULT_DATAFMT` (Bayer sai) ở đây hoặc `board_setup` |

## Cách dùng file này

Làm theo đúng thứ tự (đã sắp dễ → khó). Với mỗi hàm:
1. Mở `nv_ov9281.c` gốc tại dòng ghi trong bảng, đọc nội dung thật.
2. Mở `nv_ov5693.c` (R39.2) tại dòng tương ứng, đọc xem framework mới yêu cầu chữ ký/cấu trúc gì.
3. Viết hàm mới cho `nv_ov9281.c` (bản port): **giữ logic/giá trị register của OV9281**, **đổi khung theo cấu trúc OV5693**.
4. Paste cả 2 đoạn gốc vào chat nếu muốn mình review trước khi build.

---

## PHẦN 0 — Việc làm một lần, trước khi sửa hàm nào

### 0.1. Struct chính — đổi tên nhưng gần như giữ nguyên field

`struct ov9281` (gốc) hầu như chỉ cần đổi cách nó được truy cập (qua `tegracam_get_privdata(tc_dev)` thay vì truyền thẳng con trỏ `priv`). Đọc struct gốc, so với `struct ov5693` trong `ov5693.h` — copy field nào OV5693 có mà OV9281 không có (ví dụ liên quan tegracam) vào struct mới, giữ lại field riêng OV9281 (nếu có, ví dụ liên quan OTP/fuse ID).

### 0.2. Header include

- Gốc `nv_ov9281.c` khả năng include `<media/camera_common.h>` kiểu cũ.
- Bản port cần thêm include tegracam: `<media/tegracam_core.h>` (tên chính xác — xác nhận bằng cách xem đầu file `nv_ov5693.c`).
- Bỏ include nào chỉ phục vụ `v4l2_ctrl_ops` viết tay (không cần nữa vì framework tự lo).

### 0.3. `static const struct of_device_id ov9281_of_match[]`

Đổi `compatible = "nvidia,ov9281"` (hoặc tên cũ) → **`compatible = "ovti,ov9281"`** (chuẩn theo `ovti,ov5693.yaml` binding, đúng convention `<vendor>,<part>`).

### 0.4. `ctrl_cid_list[]` — khai báo mới hoàn toàn (không có trong bản gốc)

Copy từ `nv_ov5693.c`, danh sách control ID tối thiểu cần cho sensor mono (bỏ control liên quan màu nếu OV5693 có):
```c
static const u32 ctrl_cid_list[] = {
    TEGRA_CAMERA_CID_GAIN,
    TEGRA_CAMERA_CID_EXPOSURE,
    TEGRA_CAMERA_CID_FRAME_RATE,
    TEGRA_CAMERA_CID_GROUP_HOLD,
    TEGRA_CAMERA_CID_HDR_EN, /* chỉ giữ nếu OV9281 hỗ trợ, kiểm tra lại */
    TEGRA_CAMERA_CID_SENSOR_MODE_ID,
};
```
→ Đối chiếu danh sách thật trong `nv_ov5693.c` (tìm biến `ctrl_cid_list` bằng `grep -n "ctrl_cid_list" nv_ov5693.c`), copy cấu trúc, bỏ control không áp dụng cho OV9281 (không có white balance vì sensor mono).

### 0.5. `static const struct tegracam_ctrl_ops ov9281_ctrl_ops`

Struct mới, map từng CID ở trên với hàm xử lý (sẽ viết ở Phần 2):
```c
static struct tegracam_ctrl_ops ov9281_ctrl_ops = {
    .numctrls = ARRAY_SIZE(ctrl_cid_list),
    .ctrl_cid_list = ctrl_cid_list,
    .set_gain = ov9281_set_gain,
    .set_exposure = ov9281_set_exposure,
    .set_frame_rate = ov9281_set_frame_rate,
    .set_group_hold = ov9281_set_group_hold,
};
```
Copy chính xác cấu trúc từ `nv_ov5693.c`, đổi tên hàm cho OV9281.

---

## PHẦN 1 — Cặp hàm dễ nhất: I/O cơ bản

### 1.1. `ov9281_read_reg` / `ov9281_write_reg` / `ov9281_write_table`

- **Vị trí gốc:** dòng 137, 150, 164 (`nv_ov9281.c`, R35.6.5)
- **Việc cần làm:** Chữ ký hàm dùng `struct camera_common_data *s_data` — **rất có thể giữ nguyên gần như 100%**, vì đây là API tầng thấp ít đổi giữa framework v1/v2.0.
- **Kiểm tra kỹ:** `camera_common_i2c_read_reg`/`camera_common_i2c_write_reg` (hàm helper bên trong) có đổi tên/tham số giữa kernel 5.10 và 6.8 không — so `nv_ov5693.c` dòng tương ứng (119, 131, 145) để xác nhận.
- **Việc port:** copy gần như nguyên bản, chỉ sửa nếu compiler báo lỗi type mismatch.

### 1.2. `ov9281_i2c_addr_assign` (dòng 183, gốc) — ✅ ĐÃ ĐỌC CODE THẬT, đã chốt cách xử lý

- Hàm này **không có trong `nv_ov5693.c`** — xác nhận đây đúng là tính năng riêng OV9281.
- **Đã đọc nội dung thật:** đây là cơ chế gán địa chỉ I2C **động qua GPIO** (`priv->cam_sid_gpio`), dùng khi nhiều sensor OV9281 cùng loại chia sẻ 1 bus I2C — kéo GPIO SID xuống 0 để sensor phản hồi ở `0xC0`/`0x60`, ghi địa chỉ mới vào register `0x302B` (SCCB_ID), rồi kéo GPIO lên 1 để "im lặng" trước khi chuyển sang sensor tiếp theo.
- **✅ QUYẾT ĐỊNH: giữ nguyên hàm này khi port (thể hiện đọc hiểu đầy đủ chip), nhưng KHÔNG phụ thuộc vào nó để dự án chạy.** Phương án chính thức của dự án là tách 2 camera vào **2 bus I2C vật lý riêng** (giống `imx219-dual.dts`) — cách này không cần biết/đo chân SID vật lý trên module, không đụng độ địa chỉ dù cả 2 sensor đều ở `0x60` mặc định.
- **Việc port cụ thể:** đổi tham số `struct ov9281 *priv` → theo khuôn tegracam (lấy `priv` qua `tegracam_get_privdata(tc_dev)` bên trong, tương tự các hàm khác), gọi hàm này bên trong `ov9281_power_on()` mới đúng như bản gốc đã làm (xem 2.1 bên dưới) — nhưng vì mỗi camera có bus I2C riêng, hàm này thực chất sẽ luôn nhận `i2c_addr = OV9281_DEFAULT_I2C_ADDRESS_C0` (0x60) cho cả 2 camera, chỉ chạy nhánh đầu tiên (early return, không cần GPIO SID thật).
- Chân SID vật lý trên module Waveshare **không cần điều tra thêm** — không nằm trên đường găng của dự án nữa.

---

## PHẦN 2 — Cặp hàm trung bình: Power on/off, register set-value

### 2.1. `ov9281_power_on` / `ov9281_power_off` — ✅ ĐÃ ĐỌC CODE THẬT

- **Vị trí gốc:** dòng 278, 341
- **Vị trí khuôn mẫu:** `nv_ov5693.c` dòng 172 (`ov5693_power_on`), 233 (`ov5693_power_off`)
- **Chữ ký giữ nguyên:** cả 2 bên đều nhận `struct camera_common_data *s_data` — cặp dễ vì không đổi framework interface.
- **Đã xác nhận từ code thật:** `power_on()` KHÔNG tự tay toggle GPIO XSHUTDOWN — chỉ bật 3 regulator (`avdd`, `dvdd`, `iovdd`) qua `pw->pdata->power_on()` (callback board-cụ thể) hoặc `regulator_enable()` trực tiếp, sau đó `usleep_range(5350, 5360)` (5.35ms — đã kiểm tra: vượt xa yêu cầu tối thiểu 8192 chu kỳ XVCLK ≈ 341µs @24MHz, an toàn, giữ nguyên khi port), rồi gọi `ov9281_i2c_addr_assign()` (xem Phần 1.2).
  → **Kết luận: XSHUTDOWN được xử lý ở tầng device tree** (mô hình hóa như "gpio-regulator" ảo, tự bật cùng lúc với 1 trong 3 regulator trên) — không có logic GPIO riêng trong file `.c` cần port ở bước này. Cần `grep "gpio-regulator"` trong `.dtsi` IMX219 đang dùng để xác nhận pattern này khi viết Giai đoạn 3.
- **`power_off()` đã xác nhận:** chủ động gọi `ov9281_write_table(priv, ov9281_mode_table[OV9281_MODE_STOP_STREAM])` (ghi `0x0100=0`, software standby) **trước khi** tắt regulator — đúng khuyến nghị datasheet mục 2.5.2. Giữ nguyên thứ tự này khi port.
- **Việc port cụ thể:** copy gần như nguyên cấu trúc, chỉ đổi cách lấy `priv` (`tegracam_get_privdata(tc_dev)` thay vì `s_data->priv` — nhưng chữ ký hàm vẫn nhận `s_data` như bản gốc, không đổi theo `tc_dev` vì OV5693 khuôn mẫu cũng giữ nguyên `s_data` cho riêng 2 hàm này). Thêm lời gọi `ov9281_i2c_addr_assign(priv, priv->i2c_client->addr)` đúng vị trí bản gốc — với thiết kế 2-bus-I2C-riêng (đã chốt), hàm này sẽ luôn nhận địa chỉ mặc định `0x60`, chỉ chạy nhánh early-return đầu tiên, không cần GPIO SID thật hoạt động.

### 2.2. `ov9281_power_put` / `ov9281_power_get`

- **Vị trí gốc:** dòng 375, 380 — tham số `struct ov9281 *priv`
- **Khuôn mẫu:** `nv_ov5693.c` dòng 275, 297 — tham số **`struct tegracam_device *tc_dev`**
- **Việc port:** đổi tham số hàm sang `tc_dev`, bên trong lấy lại `priv` bằng:
  ```c
  struct ov9281 *priv = (struct ov9281 *)tegracam_get_privdata(tc_dev);
  ```
  rồi giữ nguyên logic cấp/giải phóng regulator bên trong như bản gốc.

### 2.3. `ov9281_set_group_hold` (dòng 439, gốc)

- **Khuôn mẫu:** `ov5693_set_group_hold(struct tegracam_device *tc_dev, bool val)` dòng 379
- **Việc port:** đổi tham số từ `struct ov9281 *priv` sang `(tc_dev, bool val)`, giữ nguyên địa chỉ register group-hold cụ thể của OV9281 (đọc trong hàm gốc, đối chiếu mục 7.3 datasheet "SCCB and group hold control [0x3100~0x3107]").

### 2.4. `ov9281_set_gain` (dòng 475, gốc) — ví dụ đầu tiên "mổ" từ `s_ctrl`

- **Khuôn mẫu:** `ov5693_set_gain(struct tegracam_device *tc_dev, s64 val)` dòng 423
- **Lưu ý quan trọng:** chữ ký gốc OV9281 đã có sẵn `ov9281_set_gain(struct ov9281 *priv, s32 val)` — **đây là 1 trong các case riêng lẻ vốn được gọi từ bên trong `ov9281_s_ctrl()` (dòng 881)**, không phải hàm độc lập kiểu framework mới. Việc port: đổi tham số `priv, s32` → `tc_dev, s64` theo khuôn OV5693, giữ nguyên công thức tính giá trị gain-register cụ thể của OV9281.

### 2.5. `ov9281_set_frame_length` (dòng 512) / `ov9281_set_coarse_time` (dòng 556)

- **Khuôn mẫu tương ứng bên OV5693:** `ov5693_set_frame_rate` (dòng 459) và `ov5693_set_exposure` (dòng 497) — **tên hàm đổi khái niệm**: "frame_length"/"coarse_time" (đơn vị dòng/register thô) → "frame_rate"/"exposure" (đơn vị thời gian, framework tự quy đổi).
- **Việc port quan trọng:** đây không chỉ đổi tên — cần đọc kỹ xem `tegracam` framework có tự làm phép quy đổi từ "frame rate (fps)" sang "frame length (register value)" giùm bạn hay không (thường có sẵn qua `mode->control_properties` trong mode table). Đọc `ov5693_set_frame_rate` để xem input/output value dùng đơn vị gì, rồi viết `ov9281_set_frame_rate` theo đúng khuôn, nhưng dùng công thức chuyển đổi số dòng-register cụ thể lấy từ `ov9281_set_frame_length` gốc.

---

## PHẦN 3 — Cặp hàm quan trọng nhất: Streaming

### 3.1. `ov9281_s_stream` (dòng 672-800, gốc — khá dài, ~130 dòng) → tách thành 2 hàm

- **Khuôn mẫu:** `ov5693_start_streaming(struct tegracam_device *tc_dev)` dòng 876, `ov5693_stop_streaming(struct tegracam_device *tc_dev)` dòng 925
- **Việc port (quan trọng nhất toàn dự án):**
  1. Đọc toàn bộ `ov9281_s_stream()` gốc, xác định rõ đoạn code nào chạy khi `enable=1` (start) và đoạn nào chạy khi `enable=0` (stop) — thường có `if (enable) { ... } else { ... }`.
  2. Đoạn `if (enable)` → chuyển thành nội dung `ov9281_start_streaming(tc_dev)`.
  3. Đoạn `else` → chuyển thành nội dung `ov9281_stop_streaming(tc_dev)`.
  4. **Register cần ghi khi start:** `0x0100 = 1` (đã xác nhận từ datasheet — SC_MODE_SELECT bit[0]=1 nghĩa là streaming).
  5. **Register cần ghi khi stop:** `0x0100 = 0` (software standby).
  6. Framework tegracam **tự gọi 2 hàm này** khi cần (qua `v4l2_subdev_video_ops.s_stream` chuẩn của framework, không cần bạn tự implement `s_stream` nữa) — xác nhận bằng cách tìm trong `nv_ov5693.c` xem có `.s_stream` nào được gán thủ công không (thường không có, framework tự route).
  7. Đối chiếu ghi chú datasheet: khi stop, sensor tự đợi tới cuối frame MIPI trước khi vào standby (nếu lệnh đến giữa frame) — không cần thêm delay thủ công trong `stop_streaming()`, chỉ cần ghi `0x0100=0` đúng lúc.

### 3.2. `ov9281_g_input_status` (dòng 800) — kiểm tra còn cần không

- Hàm này thuộc `v4l2_subdev_video_ops` — kiểm tra `nv_ov5693.c` có tương đương không (`grep -n "g_input_status" nv_ov5693.c`). Nếu không có, khả năng framework mới không cần hàm này nữa — có thể bỏ, nhưng **cần xác nhận bằng cách đọc thật trước khi xóa**.

### 3.3. `ov9281_set_fmt` / `ov9281_get_fmt` (dòng 811, 825) — ✅ ĐÃ ĐỌC CODE THẬT, xác nhận bỏ được an toàn

- Đã đọc nội dung thật: cả 2 hàm chỉ là **wrapper rỗng** gọi thẳng `camera_common_try_fmt`/`camera_common_s_fmt`/`camera_common_g_fmt` (helper chung framework cũ) — **không chứa bất kỳ logic Bayer/RAW8-RAW10 riêng nào**. Xác nhận an toàn để xóa hoàn toàn khi port sang tegracam v2.0, không mất logic gì.
- **Hằng số đáng ngờ `OV9281_DEFAULT_DATAFMT = MEDIA_BUS_FMT_SBGGR10_1X10` (Bayer, sai vì OV9281 là mono) KHÔNG nằm ở đây** — vẫn cần tìm ở `board_setup()`/`probe()` (phần chưa đọc), đây là nơi thật sự cần sửa thành `MEDIA_BUS_FMT_Y10_1X10`.

---

## PHẦN 4 — Control framework: `s_ctrl` → tách hàm riêng

### 4.1. `ov9281_g_volatile_ctrl` (dòng 862) — khả năng bỏ hoàn toàn

- Không có tương đương bên OV5693 — framework tegracam v2.0 tự xử lý việc đọc lại giá trị control (volatile read-back) khác cách cũ. Đọc nội dung hàm gốc: nếu chỉ đơn giản đọc lại giá trị vừa set (không có logic đặc biệt), có thể bỏ hoàn toàn.

### 4.2. `ov9281_s_ctrl` (dòng 881-1010, ~130 dòng) — hàm quan trọng thứ 2, cần "mổ" kỹ

- Đọc toàn bộ switch-case trong hàm này. Với mỗi `case V4L2_CID_xxx:`, xác định:
  - Case tương ứng CID nào trong `ctrl_cid_list[]` mới (Phần 0.4)
  - Nội dung xử lý bên trong case đó chuyển thành 1 hàm độc lập (nếu chưa có sẵn như `ov9281_set_gain` đã tách ở Phần 2.4)
- **Ví dụ cụ thể cần làm:**
  ```
  case V4L2_CID_GAIN:
      err = ov9281_set_gain(priv, ctrl->val);   // ĐÃ CÓ hàm riêng (dòng 475)
      break;
  case V4L2_CID_EXPOSURE:
      err = ov9281_set_coarse_time(priv, ctrl->val);  // cần đổi tên thành ov9281_set_exposure khi port
      break;
  case V4L2_CID_FRAME_RATE: (nếu có)
      err = ov9281_set_frame_length(priv, ctrl->val);  // đổi tên thành ov9281_set_frame_rate
      break;
  case V4L2_CID_GROUP_HOLD:
      err = ov9281_set_group_hold(priv);  // ĐÃ CÓ hàm riêng (dòng 439)
      break;
  ```
  (Đây là suy đoán cấu trúc thường gặp — cần đọc code thật để xác nhận case cụ thể có trong `nv_ov9281.c`.)

### 4.3. `ov9281_ctrls_init` (dòng 1010) — bỏ, thay bằng khai báo tĩnh

- Không cần nữa — framework tự khởi tạo control từ `ctrl_cid_list[]` (Phần 0.4) + `ov9281_ctrl_ops` (Phần 0.5). Đọc hàm gốc để chắc chắn không bỏ sót control nào chưa liệt kê ở Phần 0.4 (ví dụ default value, min/max range riêng của OV9281 — range này cần chuyển vào mode table hoặc struct riêng, kiểm tra cách OV5693 làm).

---

## PHẦN 5 — Setup & Probe (làm cuối cùng)

### 5.1. `ov9281_parse_dt` (dòng 1068)

- **Khuôn mẫu:** `ov5693_parse_dt(struct tegracam_device *tc_dev)` dòng 778 — trả về `struct camera_common_pdata *` (khác gốc OV9281 dùng `struct i2c_client *client, struct ov9281 *priv` làm tham số, trả `int`).
- **Việc port:** đổi chữ ký hàm theo khuôn OV5693, giữ nguyên tên property cụ thể mà `of_property_read_*` gọi (ví dụ GPIO reset, MCLK) — đối chiếu với property thực tế sẽ khai trong `.dtsi` (project plan Giai đoạn 3).

### 5.2. `ov9281_verify_chip_id` (dòng 1131)

- Không có tương đương tên riêng bên OV5693 (có thể nằm trong `board_setup()`) — nhưng **giá trị chip ID đã xác nhận chắc chắn: đọc 0x300A/0x300B, kỳ vọng 0x92/0x81** (xem project plan). Giữ nguyên logic, chỉ cần gọi đúng chỗ trong `board_setup()`/`probe()` mới.

### 5.3. `ov9281_probe` (dòng 1166 — hàm dài nhất, làm cuối cùng)

- **Khuôn mẫu:** `ov5693_probe(struct i2c_client *client)` dòng 1149/1151 (chú ý: có 2 định nghĩa cùng tên với `#if LINUX_VERSION_CODE` guard trong `nv_imx219.c` dòng 696/698 — copy pattern này nếu cần support song song 2 API i2c).
- **Việc port lớn nhất:** viết lại toàn bộ nội dung `probe()` theo khuôn `tegracam_device_register()` thay vì cách khởi tạo v4l2_subdev thủ công kiểu cũ. Đây là hàm "tổng hợp" gọi tất cả hàm đã port ở trên — làm cuối cùng, sau khi đã có đủ:
  - `ov9281_ctrl_ops` (Phần 0.5)
  - `ov9281_start_streaming`/`stop_streaming` (Phần 3.1)
  - `ov9281_power_on/off/get/put` (Phần 2.1-2.2)
  - `ov9281_parse_dt` (Phần 5.1)

---

## Bảng tổng hợp API cũ→mới cần áp dụng xuyên suốt (nhắc lại từ project plan, dùng khi build lỗi)

| API cũ (5.10) | API mới (6.8) | Áp dụng ở đâu trong file |
|---|---|---|
| `i2c_new_client_device()` khác chữ ký | Chữ ký mới | `probe()` nếu có tạo I2C client con |
| `ktime_get_ts64`, `struct timespec64` | API mới | Bất kỳ đâu dùng timestamp (hiếm trong sensor driver, thường không có) |
| Power Domain framework cũ | Header mới `pm_domain.h` | `power_on/off` nếu dùng power domain (thường sensor driver không dùng trực tiếp) |
| `dev_err()` định nghĩa cũ | Định nghĩa mới | Toàn bộ file — thường không cần sửa gì, chỉ cần include đúng header |
| I2C lock API cũ | `i2c_lock_bus()`/`i2c_unlock_bus()` | `read_reg`/`write_reg` nếu có tự lock bus thủ công |

---

## Việc cần làm ngay tiếp theo

1. Đọc thật nội dung Phần 1 (I/O cơ bản) — đơn giản nhất, làm quen nhịp đọc/port.
2. Đọc thật Phần 2.1 (`power_on`/`power_off`) — đối chiếu với 8192/512 XVCLK cycle delay đã tính.
3. Paste code thật vào chat theo từng phần nhỏ để mình review trước khi bạn build thử.
