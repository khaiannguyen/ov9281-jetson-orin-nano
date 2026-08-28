# Tiến độ dự án: Port driver OV9281 (R35.6.5 → R39.2)

Cập nhật: 2026-08-28

## 1. Mục tiêu dự án

Port driver camera OV9281 từ Jetson Linux R35.6.5 (kernel 5.10, camera
framework v1) sang R39.2 (kernel 6.8, tegracam v2.0), chạy trên Jetson Orin
Nano với 2 camera đồng thời (cam0/cam1).

## 2. Đã hoàn thành

- **`nv_ov9281_ported_partial.c`**: port đầy đủ struct, I/O, các hàm
  `power_on`/`power_off`/`power_put`/`power_get`, `set_gain`, `set_exposure`,
  `set_frame_rate`, `group_hold`, `start_streaming`/`stop_streaming`,
  `set_mode`, OTP/fuse_id, `parse_dt`, `board_setup`, `verify_chip_id`,
  `probe`/`remove`.
- **Build test**: compile sạch, 0 lỗi kể cả với `-Werror -Wmissing-prototypes`;
  link sạch với `tegra-camera.ko` thật, 0 undefined symbol.
- **`ov9281_mode_tbls.h`**: xác nhận `sensor_mode_properties` nằm ở Device
  Tree (không phải struct C trong file này); đã tính `pix_clk_hz` /
  `line_length` cho cả 3 mode.
- **Bug thật trong code gốc NVIDIA**: phát hiện và sửa lỗi mode 640x400 bị
  gán nhầm fps 60 thay vì đúng ~210 fps — đã thêm bảng `ov9281_210fps[]`.
- **2 header cần thiết**:
  - `ov9281.h` — rỗng có chủ đích.
  - `ov9281_trace.h` — khung tracepoint không dùng, khớp cách driver
    OV5693 làm.

## 3. Quyết định kiến trúc đã chốt

- Dùng **i2c-mux-gpio** (1 controller I2C vật lý + GPIO mux CAM_I2C_MUX tạo
  2 bus con logic i2c@0/i2c@1) cho cam0/cam1 — **SỬA LẠI** so với ghi chú cũ
  "2 bus I2C vật lý riêng": sau khi đọc code `.dts` thật của IMX219 dual
  (R39.2, đang chạy ổn trên board), xác nhận cơ chế thật là i2c-mux-gpio,
  không phải 2 controller vật lý tách biệt. Kết quả tương đương về driver
  (2 i2c_client/i2c_adapter riêng, không đụng địa chỉ 0x60), không cần GPIO
  SID động.
- Bỏ hẳn nhánh `override_enable` (tính năng debug không cần thiết).
- MCLK thực tế = **24MHz** (đã xác nhận) — nhưng **không quản lý qua clock
  framework**: grep toàn bộ cây clock T234/BPMP (R39.2) xác nhận không tồn
  tại clock "cam_mclk1"/"cam_mclk2" nào; trên board P3768, MCLK là
  oscillator cố định luôn bật ở tầng phần cứng (khớp cách `nv_imx219.c`
  thật xử lý — chỉ gọi `devm_clk_get()` nếu DT có khai "mclk", mặc định thì
  không). Đã sửa `ov9281_power_get()` (thêm guard `if (pdata->mclk_name)`)
  để khớp đúng — bản trước có bug thật sẽ làm `probe()` FAIL ở Giai đoạn 4
  (`devm_clk_get(dev,"cam_mclk1")` luôn trả -ENOENT).
- Tương tự, **không có regulator riêng** (avdd/dvdd/iovdd) cho khe 22-pin
  trên board P3768 — nguồn cố định luôn bật ở tầng phần cứng, IMX219 thật
  cũng không khai `avdd-reg`/`dvdd-reg`/`iovdd-reg`. `.dtsi` OV9281 theo
  đúng pattern này.
- `stop_streaming` không thêm delay chờ hết frame — theo datasheet, sensor
  tự xử lý việc này.

## 4. Giai đoạn 3 — Device tree overlay: ✅ ĐÃ XONG (file thật, compile sạch)

- `~/Downloads/tegra234-camera-ov9281-dual.dtsi` — nội dung sensor (mode0/
  mode1/mode2 cho cả cam0/cam1), dùng khung `tegra234-camera-rbpcv2-
  imx219.dtsi` (R39.2 thật) làm nền.
- `~/Downloads/tegra234-p3767-camera-p3768-ov9281-dual.dts` — overlay
  wrapper (i2c-mux-gpio, GPIO reset, compatible string), dùng khung
  `tegra234-p3767-camera-p3768-imx219-dual.dts` (R39.2 thật) làm nền.
- Đã compile thử bằng `cpp` + `dtc -@` thật (headers thật từ
  `~/Downloads/public_sources_39.2`) → **0 lỗi**, cùng bộ warning y hệt
  file IMX219 gốc của NVIDIA (xác nhận warning là đặc tính vốn có của kiểu
  overlay này, không phải lỗi mới).
- Số liệu mode (pix_clk_hz=80MHz, line_length=728, gain_factor=16,
  max_exp_time theo công thức "VTS-25 dòng" từ datasheek 2.5.2) lấy từ
  block tính toán đã có sẵn trong `ov9281_mode_tbls.h`. `pixel_t="RAW10"`
  dùng làm placeholder hợp lệ gần nhất (sensor_common.c không có entry
  mono/Y10 thật — không ảnh hưởng mbus code thật gửi userspace, đã port
  đúng `MEDIA_BUS_FMT_Y10_1X10` trong `.c`).

## 4b. Giai đoạn 4 — Build & cài thử (Bước 1-4/5, chưa reboot) ✅ ĐÃ XONG (2026-08-28)

- Backup đầy đủ trước khi đụng gì: `~/Downloads/backup_20260828_before_ov9281_install/`
  (extlinux.conf, base DTB, overlay IMX219 đang dùng, modinfo nv_ov5693,
  Makefile gốc trước khi sửa).
- `.dtbo` overlay OV9281 build thật (không phải test) tại
  `~/Downloads/ov9281_build/tegra234-p3767-camera-p3768-ov9281-dual.dtbo`
  (11500 bytes, Device Tree Blob hợp lệ) — **chưa copy vào `/boot`**.
- **`nv_ov9281.ko` build thành công** qua đúng quy trình build chính thức
  R39.2 (`Linux_for_Tegra/source/Makefile`, target `modules`, cần
  `kernel_name=noble` — phát hiện quan trọng: nếu build trực tiếp
  `nvidia-oot/Makefile` đứng riêng hoặc quên `kernel_name`, thiếu header
  `nvidia/conftest.h` tự sinh + 2 driver không liên quan `ivc_ext`/`tegra_hv`
  bị build trùng symbol với `vmlinux` gây lỗi — đã chẩn đoán và sửa đúng gốc,
  không phải OV9281 có lỗi). File nằm ở
  `nvidia-oot/drivers/media/i2c/nv_ov9281.ko`, vermagic khớp chính xác
  `6.8.12-1021-tegra` đang chạy.
- `insmod`/`modprobe` test sạch — không lỗi symbol, chỉ có dòng "module
  verification failed... tainting kernel" (cảnh báo chuẩn cho module build
  tay không ký, không phải lỗi). Đã cài vào
  `/lib/modules/6.8.12-1021-tegra/updates/drivers/media/i2c/nv_ov9281.ko` +
  `depmod -a`.
- **Bước 5 (5a-5c) ✅ ĐÃ XONG** (2026-08-28, user đã xác nhận tiếp tục): đã
  copy `.dtbo` vào `/boot/tegra234-p3767-camera-p3768-ov9281-dual.dtbo`,
  backup `extlinux.conf` → `extlinux.conf.bak_ov9281`, thêm `LABEL
  OV9281Dual` mới vào cuối file (chỉ APPEND, không sửa/xóa gì phần cũ — đã
  verify bằng `diff`). `DEFAULT` vẫn là `JetsonIO` (IMX219) —
  `grep -c "^LABEL"` = 3. **CHƯA reboot** — user sẽ tự chạy `sudo reboot`
  và tự chọn entry `OV9281Dual` khi ngồi trước console vật lý (HDMI+bàn
  phím/UART), vì cần thấy màn hình lúc boot để chọn đúng entry trong 30s.
- Kế hoạch chi tiết + rollback đầy đủ: `~/.claude/plans/quirky-swimming-shamir.md`.

## 5. Còn treo (chưa làm)

- ~~**GPIO reset (XSHUTDOWN)**~~ ✅ ĐÃ ĐÓNG (2026-08-28): dump device tree
  ĐANG CHẠY THẬT (`sudo dtc -I fs /proc/device-tree -O dts`), giải mã
  `reset-gpios` thật trên node IMX219 đang chạy → khớp chính xác
  `TEGRA234_MAIN_GPIO(H,6)` (cam0) và `TEGRA234_MAIN_GPIO(AC,0)` (cam1) —
  đúng 2 chân đã dùng trong `.dts`. Đây là chân của carrier board (đầu nối
  22-pin CSI), không phụ thuộc loại sensor cắm vào cam0/cam1 slot nào.
  Không cần đo multimeter nữa — đã verify bằng chính device tree thật.
- **max_gain_val** trong `.dtsi` (256, ~16x) là ước lượng an toàn — datasheet
  không công bố dynamic range/max gain cụ thể ("TBD"), cần tinh chỉnh khi
  bring-up thật.
- **Giai đoạn 4**: build/flash thật lên board (cross-compile + DTB) — khác
  với compile test trên host đã làm ở mục 2 và 4.
- **Giai đoạn 5-6**: bring-up vật lý, capture ảnh thật, chạy 2 camera đồng
  thời.

## 7. BG10 sai nhãn format V4L2 — đã điều tra nguyên nhân, HOÃN sửa (2026-08-28)

- **Hiện tượng**: sau reboot sạch (2 camera bound thành công, 2 cảnh báo cũ
  đã biến mất — xác nhận Hướng 1 gain fix giải quyết triệt để),
  `v4l2-ctl --list-formats-ext` báo format `'BG10'` (10-bit Bayer BGBG/GRGR)
  — SAI, vì OV9281 là sensor MONO, không có color filter array.
- **Nguyên nhân gốc (đã trace bằng code, không suy đoán)**: `.dtsi`
  (`tegra234-camera-ov9281-dual.dtsi`, cả 6 mode node/2 camera) đặt
  `pixel_t = "RAW10"`. Chuỗi này chảy qua:
  `sensor_common.c: extract_pixel_format()` (dòng 284-285, string-match
  thẳng `"RAW10"` → `V4L2_PIX_FMT_SBGGR10`) → `tegracam_core.c:152-153`
  (`camera_common_find_pixelfmt()`) → `camera_common.c:
  camera_common_color_fmts[]` (tra bảng ra `MEDIA_BUS_FMT_SBGGR10_1X10`) →
  `vi5_formats.h: vi5_video_formats[]` (entry `TEGRA_VIDEO_FORMAT(RAW10, ...
  SBGGR10, "BGBG.. GRGR..")` — chính là cái `v4l2-ctl` in ra "BG10").
  `OV9281_DEFAULT_DATAFMT` (`nv_ov9281.c:109`, đã sửa thành
  `MEDIA_BUS_FMT_Y10_1X10` từ đầu quá trình port) xác nhận **chết hoàn
  toàn** — chỉ xuất hiện trong `#define`/comment, không chỗ nào gọi tới.
- **Phát hiện quan trọng**: không có cách nào sửa đúng chỉ bằng `.dtsi`.
  Cả 3 bảng framework dùng chung (`extract_pixel_format()` whitelist,
  `camera_common_color_fmts[]`, `vi5_video_formats[]`) đều **không có bất
  kỳ entry mono/Y10/GREY nào** trong toàn cây `nvidia-oot` R39.2 — đối
  chiếu 2 driver còn lại (`nv_ar0234.c`, `nv_hawk_owl.c`) cũng không có
  sensor mono nào để tham khảo cách khai đúng. OV9281 sẽ là sensor mono
  đầu tiên trong cây này. IMX219 (Bayer thật, đối chiếu) dùng
  `mode_type="bayer"` + `pixel_phase="rggb"` + `csi_pixel_bit_depth="10"`
  (không dùng `pixel_t` trực tiếp) → ghép chuỗi `"bayer_rggb10"` khớp
  whitelist.
- **Sửa đúng cần cả 4 file phối hợp** (3 file framework dùng chung +
  `.dtsi`): `sensor_common.c` (thêm nhánh mono vào
  `extract_pixel_format()`), `camera_common.c` (thêm entry
  `{MEDIA_BUS_FMT_Y10_1X10, V4L2_COLORSPACE_RAW, V4L2_PIX_FMT_Y10}` vào
  `camera_common_color_fmts[]`), `vi5_formats.h` (thêm
  `TEGRA_VIDEO_FORMAT` cho Y10 mono, tái dùng `T_R16` làm `img_fmt`), và
  `.dtsi` (đổi `pixel_t`).
- **QUYẾT ĐỊNH (2026-08-28)**: **HOÃN** sửa 3 file framework —
  `sensor_common.c`/`camera_common.c` build chung vào `tegra-camera.ko`,
  `vi5_formats.h` vào `nvhost_vi5.ko`, cả hai đang loaded và có IMX219
  đang chạy tốt phụ thuộc vào (rủi ro cao hơn nhiều so với các lần sửa
  `nv_ov9281.ko` riêng lẻ trước đây). Nhãn `BG10` sai không tự nó chặn
  việc capture — dữ liệu thô qua CSI là byte thật, không đổi theo nhãn
  V4L2. Việc mở rộng framework để có nhãn đúng sẽ làm sau, sau khi xác
  nhận capture ảnh OK.

## 8. Thử capture thật trên cam0 (/dev/video0) — THẤT BẠI, blocker MỚI (2026-08-28)

- **Lệnh chạy**: `v4l2-ctl --set-fmt-video=width=1280,height=800,pixelformat=BG10
  --stream-mmap --stream-count=1 -d /dev/video0
  --stream-to=ov9281_cam0_test.raw`
- **Kết quả**: treo, không có output, không có frame nào về. Phải
  `kill -INT` sau ~3 phút chờ. **File output = 0 byte** — không có dữ
  liệu thô nào được ghi ra, kể cả 1 frame.
- **dmesg (bằng chứng thật, không suy đoán)**: pipeline VI/NVCSI cấu hình
  xong bình thường (`[PIPELINE 0] VI capture setup complete
  (channel_id=0, csi_stream=1, vc=0)`, `pixel format=VI_PIXFMT_FORMAT_T_R16`,
  PHY/CIL/lane config ghi log đầy đủ, không lỗi), nhưng ngay sau đó:
  ```
  tegra-camrtc-capture-vi tegra-capture-vi: uncorr_err: request timed out after 2500 ms
  ```
  lặp lại 4 lần (13:23:06 → 13:23:13) rồi ngừng hẳn (RCE bỏ cuộc), trong
  khi tiến trình `v4l2-ctl` vẫn treo vô thời hạn ở `DQBUF` chờ frame
  không bao giờ tới.
- **Ý nghĩa**: đây là lỗi tầng khác hẳn BG10 — CSI PHY/NVCSI phía Tegra
  cấu hình xong nhưng **không nhận được bất kỳ dữ liệu MIPI CSI nào từ
  sensor**. `ov9281_start_streaming()` (nv_ov9281.c:952-955, ghi bảng
  `ov9281_mode_table[OV9281_MODE_START_STREAM]`) không báo lỗi trong
  dmesg (nếu I2C write fail sẽ có dòng "error starting stream" — không
  thấy), tức lệnh "stream on" qua I2C có vẻ đã gửi thành công, nhưng
  sensor có thể không thực sự xuất dữ liệu MIPI (hoặc timing/lane/PLL
  không khớp, hoặc thiếu 1 bước init).
- **Đã loại trừ 1 giả thuyết**: fsync master/slave. `.dtsi` hiện KHÔNG có
  property `fsync` → driver mặc định `OV9281_FSYNC_NONE`
  (`nv_ov9281.c:1504`) → không phải trường hợp cam0 kẹt chờ trigger từ
  cam1 (fsync slave). Chưa xác định nguyên nhân thật.
- **CHƯA làm**: chưa viết script Python/PNG (không có dữ liệu để đọc —
  file 0 byte). Chưa test `/dev/video1` (cam1). Đây là **blocker mới,
  ưu tiên cao hơn** vấn đề nhãn BG10 — cần điều tra thêm trước khi có
  thể xác nhận "dữ liệu thô đúng" như giả thuyết ban đầu.

## 9. Vị trí file quan trọng

- `~/Downloads/nv_ov9281_ported_partial.c`
- `~/Downloads/ov9281.h`
- `~/Downloads/ov9281_trace.h`
- `~/Downloads/tegra234-camera-ov9281-dual.dtsi`
- `~/Downloads/tegra234-p3767-camera-p3768-ov9281-dual.dts`
- `~/Downloads/OV9281-Jetson-Project-Plan.md`
- `~/Downloads/OV9281-nv_ov9281-Migration-Guide.md`
