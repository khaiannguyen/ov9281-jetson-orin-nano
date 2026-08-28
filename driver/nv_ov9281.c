// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2016-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * nv_ov9281.c - ov9281 sensor driver
 *
 * PORTED: kernel 5.10 (v1 framework) -> kernel 6.8 (tegracam v2.0)
 * Nguồn gốc: R35.6.5 nv_ov9281.c (NVIDIA, 2016-2022)
 * Khung tham chiếu: R39.2 nv_ov5693.c (NVIDIA, 2013-2025)
 */

#include <nvidia/conftest.h>

#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/debugfs.h>

#include <linux/seq_file.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <media/tegra-v4l2-camera.h>
#include <media/tegracam_core.h>
#include <media/ov9281.h>              /* TODO: tạo header này, giống ov5693.h */

#include "../platform/tegra/camera/camera_gpio.h"
#include "ov9281_mode_tbls.h"
#define CREATE_TRACE_POINTS
#include <trace/events/ov9281.h>       /* TODO: tạo trace header, giống ov5693 */

/* ===================== OV9281 Registers (giữ nguyên từ bản gốc) ===================== */
#define OV9281_SC_MODE_SELECT_ADDR		0x0100
#define OV9281_SC_MODE_SELECT_STREAMING		0x01
#define OV9281_SC_CHIP_ID_HIGH_ADDR		0x300A
#define OV9281_SC_CHIP_ID_LOW_ADDR		0x300B
#define OV9281_SC_CTRL_SCCB_ID_ADDR		0x302B
#define OV9281_SC_CTRL_3B_ADDR			0x303B
#define OV9281_SC_CTRL_3B_SCCB_ID2_NACK_EN	(1 << 0)
#define OV9281_SC_CTRL_3B_SCCB_PGM_ID_EN	(1 << 1)

#define OV9281_GROUP_HOLD_ADDR			0x3208
#define OV9281_GROUP_HOLD_START			0x00
#define OV9281_GROUP_HOLD_END			0x10
#define OV9281_GROUP_HOLD_LAUNCH_LBLANK	0x60
#define OV9281_GROUP_HOLD_LAUNCH_VBLANK	0xA0
#define OV9281_GROUP_HOLD_LAUNCH_IMMED		0xE0
#define OV9281_GROUP_HOLD_BANK_0		0x00
#define OV9281_GROUP_HOLD_BANK_1		0x01

#define OV9281_EXPO_HIGH_ADDR			0x3500
#define OV9281_EXPO_MID_ADDR			0x3501
#define OV9281_EXPO_LOW_ADDR			0x3502

#define OV9281_GAIN_SHIFT_ADDR			0x3507
#define OV9281_GAIN_HIGH_ADDR			0x3508
#define OV9281_GAIN_LOW_ADDR			0x3509

#define OV9281_TIMING_VTS_HIGH_ADDR		0x380E
#define OV9281_TIMING_VTS_LOW_ADDR		0x380F
#define OV9281_TIMING_FORMAT1			0x3820
#define OV9281_TIMING_FORMAT1_VBIN		(1 << 1)
#define OV9281_TIMING_FORMAT1_FLIP		(1 << 2)
#define OV9281_TIMING_FORMAT2			0x3821
#define OV9281_TIMING_FORMAT2_HBIN		(1 << 0)
#define OV9281_TIMING_FORMAT2_MIRROR		(1 << 2)
#define OV9281_TIMING_RST_FSIN_HIGH_ADDR	0x3826
#define OV9281_TIMING_RST_FSIN_LOW_ADDR	0x3827

#define OV9281_OTP_BUFFER_ADDR			0x3D00
#define OV9281_OTP_BUFFER_SIZE			32
#define OV9281_OTP_STR_SIZE			(OV9281_OTP_BUFFER_SIZE * 2)
#define OV9281_FUSE_ID_OTP_BUFFER_ADDR		0x3D00
#define OV9281_FUSE_ID_OTP_BUFFER_SIZE		16
#define OV9281_FUSE_ID_STR_SIZE			(OV9281_FUSE_ID_OTP_BUFFER_SIZE * 2)
#define OV9281_OTP_PROGRAM_CTRL_ADDR		0x3D80
#define OV9281_OTP_LOAD_CTRL_ADDR		0x3D81
#define OV9281_OTP_LOAD_CTRL_OTP_RD		0x01

#define OV9281_PRE_CTRL00_ADDR			0x5E00
#define OV9281_PRE_CTRL00_TEST_PATTERN_EN	(1 << 7)

/* ===================== OV9281 Other Stuffs (giữ nguyên) ===================== */
#define OV9281_DEFAULT_GAIN			0x0010 /* 1.0x real gain */
#define OV9281_MIN_GAIN				0x0001
#define OV9281_MAX_GAIN				0x1FFF

#define OV9281_DEFAULT_FRAME_LENGTH		0x071C
#define OV9281_MIN_FRAME_LENGTH			0x0001
#define OV9281_MAX_FRAME_LENGTH			0xFFFF
#define OV9281_FRAME_LENGTH_1SEC		(0x40d * 120)

#define OV9281_MIN_EXPOSURE_COARSE		0x00000001
#define OV9281_MAX_EXPOSURE_COARSE		0x000FFFFF
#define OV9281_DEFAULT_EXPOSURE_COARSE		0x00002A90

#define OV9281_MAX_WIDTH			1280
#define OV9281_MAX_HEIGHT			800

#define OV9281_DEFAULT_MODE			OV9281_MODE_1280X800
#define OV9281_DEFAULT_WIDTH			OV9281_MAX_WIDTH
#define OV9281_DEFAULT_HEIGHT			OV9281_MAX_HEIGHT

/*
 * ⚠️ ĐÃ SỬA so với bản gốc: bản gốc ghi MEDIA_BUS_FMT_SBGGR10_1X10 (Bayer)
 * — SAI vì OV9281 là sensor MONO (không có color filter array).
 * Sửa thành Y10 (grayscale 10-bit), đúng datasheet mục 2.3 (output 8/10-bit RAW mono).
 */
#define OV9281_DEFAULT_DATAFMT			MEDIA_BUS_FMT_Y10_1X10

#define OV9281_DEFAULT_CLK_FREQ			24000000  /* TODO: xác nhận lại bằng dtc dump — xem ghi chú cuối file */

#define OV9281_DEFAULT_I2C_ADDRESS_C0		(0xc0 >> 1)   /* 0x60, 7-bit */
#define OV9281_DEFAULT_I2C_ADDRESS_20		(0x20 >> 1)   /* 0x10, 7-bit */
#define OV9281_DEFAULT_I2C_ADDRESS_PROGRAMMABLE	(0xe0 >> 1)   /* 0x70, 7-bit */
/* Mảng hằng số, không có ràng buộc thứ tự. */
/*
 * KHÔNG khai TEGRA_CAMERA_CID_GROUP_HOLD ở đây: framework tegracam v2.0
 * tự thêm CID này (xem tegracam_ctrls.c), khai lại gây trùng -> EINVAL
 * ở bước init ctrls. Đối chiếu nv_ov5693.c / nv_imx219.c: cả hai đều có
 * .set_group_hold trong tegracam_ctrl_ops nhưng không liệt kê CID này
 * trong ctrl_cid_list[] -> xác nhận đây là quy tắc chung của framework.
 */
static const u32 ctrl_cid_list[] = {
	TEGRA_CAMERA_CID_GAIN,
	TEGRA_CAMERA_CID_EXPOSURE,
	TEGRA_CAMERA_CID_FRAME_RATE,
	TEGRA_CAMERA_CID_SENSOR_MODE_ID,
	TEGRA_CAMERA_CID_OTP_DATA,
	TEGRA_CAMERA_CID_FUSE_ID,
};
/*
 * ===================== struct ov9281 (đã port sang tegracam v2.0) =====================
 *
 * So với bản gốc R35.6.5, đã:
 *  - BỎ: power (camera_common_power_rail)   -> truy cập qua s_data->power thay vì priv->power
 *  - BỎ: num_ctrls                          -> framework tự tính ARRAY_SIZE(ctrl_cid_list)
 *  - BỎ: ctrl_handler, ctrls[]               -> framework tegracam tự quản lý control
 *  - BỎ: media_pad pad                      -> framework tự đăng ký media pad
 *  - BỎ: regmap                             -> dùng s_data->regmap
 *  - BỎ: pdata (camera_common_pdata)        -> gắn vào s_data->pdata thay vì lưu riêng
 *  - ĐỔI TÊN: frame_period_ms -> frame_length (đơn vị register thô, khớp cách OV5693 dùng)
 *  - THÊM: tc_dev, i2c_dev, streaming_lock, streaming (bắt buộc theo khuôn tegracam v2.0,
 *          streaming_lock đặc biệt quan trọng vì dự án chạy 2 camera đồng thời)
 *
 * GIỮ NGUYÊN (đặc trưng phần cứng thật của OV9281, không có ở OV5693):
 *  - fsync           : chân FSIN, tính năng đồng bộ 2 camera qua phần cứng
 *  - cam_sid_gpio    : GPIO điều khiển chân SID, dùng trong ov9281_i2c_addr_assign()
 *
 * ĐÃ ĐỌC CODE THẬT (ov9281_parse_dt, dòng gốc 1068 + nơi dùng dòng 747-764):
 *  - mirror/flip KHÔNG còn là field riêng ở đây nữa. Bản gốc ghi priv->mirror
 *    (-> TIMING_FORMAT2, mirror ngang) và priv->flip (-> TIMING_FORMAT1, flip dọc)
 *    — đúng ngữ nghĩa 2 field CÓ SẴN trong camera_common_pdata: h_mirror/v_flip.
 *    Lý do đổi: ov9281_parse_dt() (tegracam v2.0) chạy BÊN TRONG
 *    tegracam_device_register(), TRƯỚC khi tegracam_set_privdata() gán priv
 *    cho tc_dev/s_data — nên priv chưa tồn tại lúc đó, không thể ghi trực tiếp
 *    vào priv->mirror/flip như bản gốc. Giải pháp: lưu vào s_data->pdata->h_mirror
 *    /v_flip (do parse_dt() trả về), đọc lại từ đó ở nơi ghi register sau này
 *    (Phần 3.1, set_mode/start_streaming) thay vì đọc priv->mirror/priv->flip.
 *
 * TODO (Phần 5.3, board_setup()/probe() — CHƯA LÀM): fsync, cam_sid_gpio,
 *    mcu_boot_gpio, mcu_reset_gpio đều là property DT riêng của OV9281, không
 *    có chỗ chứa trong camera_common_pdata chuẩn. Cùng lý do trên (priv chưa
 *    tồn tại lúc parse_dt chạy), 4 property này KHÔNG được đọc trong
 *    ov9281_parse_dt() nữa (khác bản gốc). Cần đọc lại bằng of_get_named_gpio()/
 *    of_property_read_string() một lần nữa ở board_setup() (giống cách OV5693
 *    đọc OTP/fuse ID ở đó, lúc priv đã có) — riêng mcu_boot_gpio/mcu_reset_gpio
 *    đã xác nhận đọc code thật: dùng để reset 1 MCU trên board ngay trong
 *    probe() (dòng gốc 1237-1244), có thể đọc thẳng trong probe() không cần
 *    lưu vào priv nếu chỉ dùng 1 lần.
 */
struct ov9281 {
	struct i2c_client		*i2c_client;
	struct v4l2_subdev		*subdev;

	int				fsync;
	int				cam_sid_gpio;

	/* TODO: xác nhận có còn dùng không khi đọc probe()/parse_dt() */
	int				mcu_boot_gpio;
	int				mcu_reset_gpio;

	s32				group_hold_prev;
	bool				group_hold_en;
	u32				frame_length;		/* trước là frame_period_ms */

	u8				otp_buf[OV9281_OTP_BUFFER_SIZE];
	u8				fuse_id[OV9281_FUSE_ID_OTP_BUFFER_SIZE];

	struct mutex			streaming_lock;
	bool				streaming;

	struct camera_common_i2c	i2c_dev;
	struct camera_common_data	*s_data;
	struct tegracam_device		*tc_dev;
};

/* =====================================================================
 * ov9281_regmap_config — port từ dòng gốc 171. GIỮ NGUYÊN giá trị gốc OV9281
 * (khác OV5693 — bản OV5693 đơn giản hơn, không cache, không single_read/write)
 * vì đây là đặc tính I2C/SCCB thật của chip OV9281, không phải chi tiết
 * framework. Đã BỎ nhánh "#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)"
 * của bản gốc — dự án chỉ nhắm kernel 6.8 (R39.2), nhánh cũ (use_single_rw,
 * cho kernel < 5.4) không bao giờ được chọn, giữ lại chỉ là dead code.
 *
 * Trong tegracam v2.0, không tự devm_regmap_init_i2c() trong probe() như bản
 * gốc nữa — đã đọc code thật tegracam_core.c:107-108, framework tự làm việc
 * này bên trong tegracam_device_register(), dùng đúng struct này qua
 * tc_dev->dev_regmap_config (gán ở ov9281_probe()).
 * ===================================================================== */
static const struct regmap_config ov9281_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
};

/* ===================== Register/regmap stuff (đã port) =====================
 *
 * So với bản gốc: regmap không còn nằm trong priv nữa, lấy thẳng từ s_data->regmap
 * (đúng cách nvidia-oot/nv_ov5693.c làm — regmap được tegracam framework quản lý
 * tập trung trong camera_common_data).
 */
static int ov9281_read_reg(struct camera_common_data *s_data, u16 addr, u8 *val)
{
	int err = 0;
	u32 reg_val = 0;

	err = regmap_read(s_data->regmap, addr, &reg_val);
	*val = reg_val & 0xFF;

	return err;
}

static int ov9281_write_reg(struct camera_common_data *s_data, u16 addr, u8 val)
{
	int err;
	struct device *dev = s_data->dev;

	err = regmap_write(s_data->regmap, addr, val);
	if (err)
		dev_err(dev, "%s: i2c write failed, 0x%x = %x\n",
			__func__, addr, val);

	return err;
}

static int ov9281_write_table(struct ov9281 *priv, const ov9281_reg table[])
{
	struct camera_common_data *s_data = priv->s_data;

	return regmap_util_write_table_8(s_data->regmap,
					 table,
					 NULL, 0,
					 OV9281_TABLE_WAIT_MS,
					 OV9281_TABLE_END);
}

/* =====================================================================
 * ov9281_i2c_addr_assign — GIỮ NGUYÊN LOGIC TỪ BẢN GỐC
 *
 * Đây là tính năng thật của chip OV9281: gán địa chỉ I2C động qua GPIO SID,
 * dùng khi nhiều sensor cùng loại share 1 bus I2C.
 *
 * QUYẾT ĐỊNH DỰ ÁN: giữ nguyên hàm này (thể hiện hiểu đầy đủ datasheet/chip),
 * NHƯNG không phụ thuộc vào nó để dự án chạy — vì thiết kế device tree dùng
 * 2 BUS I2C VẬT LÝ RIÊNG cho cam0/cam1 (giống imx219-dual.dts), nên cả 2 camera
 * sẽ luôn nhận i2c_addr = OV9281_DEFAULT_I2C_ADDRESS_C0 (0x60) và chỉ chạy
 * nhánh early-return đầu tiên bên dưới — không cần cam_sid_gpio thật hoạt động.
 *
 * Nội dung hàm giữ y hệt bản gốc, chỉ đổi cách lấy priv (xem ghi chú trong power_on).
 * ===================================================================== */
static int ov9281_i2c_addr_assign(struct ov9281 *priv, u8 i2c_addr)
{
	struct device *dev = &priv->i2c_client->dev;
	struct i2c_msg msg;
	unsigned char data[3];
	int err = 0;

	/*
	 * It seems that the way SID works for the OV9281 I2C slave address is
	 * that:
	 *
	 * SID 0 = 0xc0, 0xe0
	 * SID 1 = 0x20, 0xe0
	 *
	 * Address 0xe0 is programmable via register 0x302B
	 * (OV9281_SC_CTRL_SCCB_ID_ADDR).
	 *
	 * So, the scheme to assign addresses to an (almost) arbitrary
	 * number of sensors is to consider 0x20 to be the "off" address.
	 * Start each sensor with SID as 1 so that they appear to be off.
	 *
	 * Then, to assign an address to one sensor:
	 *
	 * 0. Set corresponding SID to 0 (now only that sensor responds
	 *    to 0xc0).
	 * 1. Use 0xc0 to program the address from the default programmable
	 *    address of 0xe0 to the new address.
	 * 2. Set corresponding SID back to 1 (so it no longer responds
	 *    to 0xc0).
	 *
	 * DỰ ÁN NÀY: mỗi camera có bus I2C riêng -> luôn rơi vào nhánh đầu tiên
	 * (i2c_addr == OV9281_DEFAULT_I2C_ADDRESS_C0), không cần cam_sid_gpio thật.
	 */

	if (i2c_addr == OV9281_DEFAULT_I2C_ADDRESS_C0) {
		dev_info(dev, "Using default I2C address 0x%02x\n", i2c_addr);
		if (gpio_is_valid(priv->cam_sid_gpio)) {
			gpio_set_value(priv->cam_sid_gpio, 0);
			msleep_range(1);
		}
		return 0;
	} else if (i2c_addr == OV9281_DEFAULT_I2C_ADDRESS_20) {
		dev_info(dev, "Using default I2C address 0x%02x\n", i2c_addr);
		if (gpio_is_valid(priv->cam_sid_gpio)) {
			gpio_set_value(priv->cam_sid_gpio, 1);
			msleep_range(1);
		}
		return 0;
	} else if (i2c_addr == OV9281_DEFAULT_I2C_ADDRESS_PROGRAMMABLE) {
		dev_info(dev, "Using default I2C address 0x%02x\n", i2c_addr);
		return 0;
	}

	/*
	 * From this point on, we are trying to program the programmable
	 * slave address.  We necessarily need to have a cam-sid-gpio for this.
	 */
	if (!gpio_is_valid(priv->cam_sid_gpio)) {
		dev_err(dev, "Missing cam-sid-gpio, cannot program I2C addr\n");
		return -EINVAL;
	}

	gpio_set_value(priv->cam_sid_gpio, 0);
	msleep_range(1);

	/*
	 * Have to make the I2C message manually because we are using a
	 * different I2C slave address for this transaction, rather than
	 * the one in the device tree for this device.
	 */
	data[0] = (OV9281_SC_CTRL_SCCB_ID_ADDR >> 8) & 0xff;
	data[1] = OV9281_SC_CTRL_SCCB_ID_ADDR & 0xff;
	data[2] = ((i2c_addr) << 1) & 0xff;

	msg.addr = OV9281_DEFAULT_I2C_ADDRESS_C0;
	msg.flags = 0;
	msg.len = 3;
	msg.buf = data;

	if (i2c_transfer(priv->i2c_client->adapter, &msg, 1) != 1) {
		dev_err(dev, "Error assigning I2C address to 0x%02x\n",
			i2c_addr);
		err = -EIO;
	}

	gpio_set_value(priv->cam_sid_gpio, 1);
	msleep_range(1);

	return err;
}

/* =====================================================================
 * ov9281_gpio_set — hàm mới, copy khuôn từ ov5693_gpio_set() (camera_gpio.h)
 * Cần thiết vì bản port dưới đây sẽ tự toggle GPIO reset (bản gốc OV9281
 * KHÔNG có đoạn này — xem ghi chú trong power_on).
 * ===================================================================== */
static void ov9281_gpio_set(struct camera_common_data *s_data,
			    unsigned int gpio, int val)
{
	struct camera_common_pdata *pdata = s_data->pdata;

	if (pdata && pdata->use_cam_gpio)
		cam_gpio_ctrl(s_data->dev, gpio, val, 1);
	else
		gpio_set_value(gpio, val);
}

/* =====================================================================
 * ov9281_power_on / ov9281_power_off — đã port (BẢN SỬA LẦN 2)
 *
 * So với bản trước gửi bạn, đã SỬA 3 điểm sai/thiếu:
 *  1. pw = s_data->power  (con trỏ có sẵn, KHÔNG lấy địa chỉ &s_data->power)
 *  2. pdata = s_data->pdata (lấy thẳng, không qua priv/tegracam_get_privdata)
 *  3. THÊM đoạn toggle GPIO reset/pwdn (bản gốc OV9281 thiếu — xem ghi chú dưới)
 *
 * GIỮ NGUYÊN từ bản gốc OV9281 (đã xác nhận đúng theo datasheet):
 *  - Thứ tự bật regulator: avdd -> dvdd -> iovdd
 *  - usleep_range(5350, 5360) trước i2c_addr_assign (v.n vượt xa yêu cầu tối
 *    thiểu 8192 chu kỳ XVCLK ~341µs @24MHz)
 *  - Gọi ov9281_i2c_addr_assign() sau khi nguồn ổn định
 *  - power_off(): ghi 0x0100=0 (STOP_STREAM) TRƯỚC KHI tắt nguồn
 *
 * ⚠️ VẤN ĐỀ MỞ — CẦN BẠN XÁC NHẬN:
 * Code gốc ov9281_power_on/off (R35.6.5) HOÀN TOÀN KHÔNG toggle GPIO reset/pwdn
 * nào — chỉ bật regulator rồi gọi i2c_addr_assign. Trong khi ov5693_power_on/off
 * (khuôn mẫu R39.2) có toggle pw->pwdn_gpio và pw->reset_gpio rõ ràng, kèm đúng
 * delay theo datasheet OV5693 mục 2.9 ("~2ms settling time").
 *
 * 2 khả năng: (a) driver OV9281 gốc 2016 thiếu sót phần này (hoặc dựa hoàn toàn
 * vào callback pdata->power_on() board-specific để xử lý GPIO, nằm ngoài file
 * này), hoặc (b) một số board tích hợp sẵn nguồn với XSHUTDOWN qua mạch ngoài.
 *
 * QUYẾT ĐỊNH: bản port dưới đây CHỦ ĐỘNG THÊM đoạn toggle pw->reset_gpio
 * (ánh xạ tới chân XSHUTDOWN của OV9281), copy khuôn timing từ OV5693 nhưng
 * ĐỔI delay theo đúng datasheet OV9281 mục 2.5.1: cần tối thiểu 8192 chu kỳ
 * XVCLK (~341µs @24MHz, dùng usleep_range(350, 360) để có buffer an toàn)
 * SAU KHI XSHUTDOWN lên cao, TRƯỚC giao dịch SCCB đầu tiên (tức i2c_addr_assign).
 * Không dùng pwdn_gpio riêng vì OV9281 chỉ có 1 chân XSHUTDOWN (không có PWDN
 * tách biệt như OV5693) — cần xác nhận lại khi đọc .dtsi/parse_dt thật.
 * ===================================================================== */
static int ov9281_power_on(struct camera_common_data *s_data)
{
	/* priv lấy qua s_data->priv — giữ nguyên cách bản gốc OV9281 làm, vì
	 * ov5693_power_on/off KHÔNG cần priv (không có tính năng i2c_addr_assign
	 * riêng) nên không dùng làm ví dụ đối chiếu được ở điểm này. Cần xác nhận
	 * lại khi đọc probe()/tegracam_device_register() thật xem field s_data->priv
	 * còn tồn tại nguyên vẹn trong tegracam v2.0 hay đã đổi cách khác.
	 */
	struct ov9281 *priv = (struct ov9281 *)s_data->priv;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	int err = 0;

	dev_dbg(dev, "%s: power on\n", __func__);

	if (pdata && pdata->power_on) {
		err = pdata->power_on(pw);
		if (err)
			dev_err(dev, "%s failed.\n", __func__);
		else
			pw->state = SWITCH_ON;
		return err;
	}

	if (pw->avdd) {
		err = regulator_enable(pw->avdd);
		if (err)
			goto ov9281_avdd_fail;
	}

	if (pw->dvdd) {
		err = regulator_enable(pw->dvdd);
		if (err)
			goto ov9281_dvdd_fail;
	}

	if (pw->iovdd) {
		err = regulator_enable(pw->iovdd);
		if (err)
			goto ov9281_iovdd_fail;
	}

	/* XSHUTDOWN lên cao — ánh xạ qua pw->reset_gpio (TODO: xác nhận tên
	 * property .dtsi thật khi viết Giai đoạn 3, có thể là reset-gpios)
	 */
	if (gpio_is_valid(pw->reset_gpio))
		ov9281_gpio_set(s_data, pw->reset_gpio, 1);

	/* datasheet 2.5.1: tối thiểu 8192 chu kỳ XVCLK (~341µs @24MHz)
	 * trước giao dịch SCCB đầu tiên — dùng 350-360µs cho có buffer an toàn
	 */
	usleep_range(350, 360);

	usleep_range(5350, 5360);

	err = ov9281_i2c_addr_assign(priv, priv->i2c_client->addr);
	if (err)
		goto addr_assign_fail;

	pw->state = SWITCH_ON;
	return 0;

addr_assign_fail:
	if (gpio_is_valid(pw->reset_gpio))
		ov9281_gpio_set(s_data, pw->reset_gpio, 0);
	if (pw->iovdd)
		regulator_disable(pw->iovdd);

ov9281_iovdd_fail:
	if (pw->dvdd)
		regulator_disable(pw->dvdd);

ov9281_dvdd_fail:
	if (pw->avdd)
		regulator_disable(pw->avdd);

ov9281_avdd_fail:
	dev_err(dev, "%s failed.\n", __func__);

	return -ENODEV;
}

static int ov9281_power_off(struct camera_common_data *s_data)
{
	struct ov9281 *priv = (struct ov9281 *)s_data->priv;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	int err = 0;

	dev_dbg(dev, "%s: power off\n", __func__);
	ov9281_write_table(priv, ov9281_mode_table[OV9281_MODE_STOP_STREAM]);

	if (pdata && pdata->power_off) {
		err = pdata->power_off(pw);
		if (!err) {
			goto power_off_done;
		} else {
			dev_err(dev, "%s failed.\n", __func__);
			return err;
		}
	}

	/* datasheet 2.5.2: tối thiểu 512 chu kỳ XVCLK (~21µs @24MHz) sau
	 * giao dịch SCCB/MIPI cuối trước khi kéo XSHUTDOWN xuống
	 */
	usleep_range(21, 25);

	if (gpio_is_valid(pw->reset_gpio))
		ov9281_gpio_set(s_data, pw->reset_gpio, 0);

	if (pw->iovdd)
		regulator_disable(pw->iovdd);
	if (pw->dvdd)
		regulator_disable(pw->dvdd);
	if (pw->avdd)
		regulator_disable(pw->avdd);

	return err;

power_off_done:
	pw->state = SWITCH_OFF;
	return 0;
}

/* =====================================================================
 * ov9281_power_put — port từ dòng gốc 375, khuôn ov5693_power_put() (dòng
 * 275). Bản gốc thực ra KHÔNG làm gì cả (return 0 ngay, không free GPIO nào)
 * — đây là quyết định đã chốt ở phiên trước: chủ động sửa đúng lại theo
 * pattern OV5693 (free GPIO đã request ở power_get, tránh leak), CHỈ free
 * đúng 1 GPIO reset_gpio (khác OV5693 free cả pwdn_gpio lẫn reset_gpio) vì
 * OV9281 chỉ có 1 chân điều khiển duy nhất (XSHUTDOWN, ánh xạ reset_gpio) —
 * không có pwdn_gpio riêng như OV5693. Không port nhánh use_cam_gpio/
 * cam_gpio_deregister() của OV5693 vì ov9281_parse_dt() không đọc property
 * "cam, use-cam-gpio" (pdata->use_cam_gpio sẽ luôn là false/0), nhánh đó sẽ
 * không bao giờ chạy nên bỏ hẳn cho gọn thay vì giữ dead code.
 * ===================================================================== */
static int ov9281_power_put(struct tegracam_device *tc_dev)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;

	if (unlikely(!pw))
		return -EFAULT;

	if (gpio_is_valid(pw->reset_gpio))
		gpio_free(pw->reset_gpio);

	return 0;
}

/* =====================================================================
 * ov9281_power_get — port từ dòng gốc 380, khuôn ov5693_power_get() (dòng
 * 297).
 *
 * Thay đổi so với bản gốc:
 *  - Chữ ký: (struct ov9281 *priv) -> (struct tegracam_device *tc_dev); lấy
 *    pw qua s_data->power (con trỏ, đúng cách tegracam v2.0 cấp phát) thay vì
 *    &priv->power (bản gốc, power nằm nhúng thẳng trong priv kiểu v1 cũ).
 *  - Regulator: GIỮ NGUYÊN cả 3 avdd/dvdd/iovdd như bản gốc (quyết định đã
 *    chốt — khác OV5693 chỉ có 2, vì OV9281 cần DVDD riêng theo datasheet
 *    mục 2.5 power requirements). Property "dvdd-reg" đã parse sẵn ở
 *    ov9281_parse_dt() từ trước.
 *  - THÊM parent-clk (parentclk_name/clk_set_parent) — bản gốc OV9281 không
 *    có đoạn này, nhưng ov9281_parse_dt() đã port dùng chung
 *    camera_common_parse_clocks() với OV5693, hàm đó CÓ populate
 *    pdata->parentclk_name nếu DT khai "parent-clk" — thêm đoạn này để không
 *    bỏ phí property đã parse được, khớp "dùng ov5693 làm khuôn cấu trúc
 *    chung". Vô hại nếu DT không khai "parent-clk" (parentclk_name = NULL,
 *    nhánh if không chạy).
 *  - SỬA LỖI THẬT (phát hiện khi viết Giai đoạn 3, đối chiếu với nv_imx219.c
 *    thật trên board P3768/Orin Nano — cùng board đang chạy IMX219 baseline
 *    ổn định): bản trước của hàm này LUÔN gọi devm_clk_get(dev, mclk_name)
 *    với fallback cứng "cam_mclk1" nếu DT không khai "mclk" — copy nguyên
 *    pattern OV5693. Nhưng grep toàn bộ cây clock T234/BPMP trong R39.2
 *    (dt-bindings/clock/tegra234-clock.h, drivers/clk/tegra/) xác nhận
 *    KHÔNG hề tồn tại clock tên "cam_mclk1"/"cam_mclk2" nào — khác các board
 *    Tegra đời cũ (TX2/Xavier) nơi OV5693 gốc được viết cho. Trên P3768,
 *    MCLK là oscillator cố định trên carrier board (luôn bật, không qua
 *    clock framework) — bằng chứng: nv_imx219.c thật (đang chạy tốt trên
 *    board này) CHỈ gọi devm_clk_get() nếu pdata->mclk_name khác NULL (tức
 *    DT có khai "mclk"), và .dtsi thật của IMX219 dual KHÔNG khai "mclk".
 *    Nếu giữ nguyên bản cũ (luôn gọi devm_clk_get(dev,"cam_mclk1")),
 *    devm_clk_get() sẽ trả -ENOENT (không tìm thấy clock) → probe() FAIL
 *    hoàn toàn cho CẢ 2 camera ở Giai đoạn 4. Đã sửa: thêm guard
 *    `if (pdata->mclk_name)` giống hệt cách nv_imx219.c làm — bỏ hẳn fallback
 *    "cam_mclk1" cứng. Khớp thiết kế Giai đoạn 3: .dtsi mới KHÔNG khai
 *    property "mclk" (đúng thực tế phần cứng board này), nên nhánh này sẽ
 *    không chạy, pw->mclk giữ NULL — an toàn, giống IMX219.
 *  - THÊM gpio_request(reset_gpio) — quyết định đã chốt: bản gốc OV9281
 *    KHÔNG tự request GPIO này ở power_get (chỉ dùng thẳng qua
 *    ov9281_gpio_set() ở power_on/off mà không request trước — một lỗ hổng
 *    thực tế của driver gốc R35.6.5). Request ở đây theo đúng pattern
 *    OV5693, mức khởi tạo ban đầu = 0 (thấp) — KHÁC OV5693 dùng mức 1: đúng
 *    ngữ nghĩa XSHUTDOWN của OV9281 là active-high-enable, giữ sensor ở
 *    reset/power-down cho tới khi ov9281_power_on() chủ động kéo lên cao sau
 *    khi regulator đã ổn định (đúng trình tự datasheet mục 2.5.1). Không có
 *    pwdn_gpio riêng (giống power_put), không port nhánh use_cam_gpio.
 * ===================================================================== */
static int ov9281_power_get(struct tegracam_device *tc_dev)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = tc_dev->dev;
	const char *mclk_name;
	const char *parentclk_name;
	struct clk *parent;
	int err = 0, ret = 0;

	if (!pdata) {
		dev_err(dev, "pdata missing\n");
		return -EFAULT;
	}

	/* Sensor MCLK — CHỈ lấy qua clock framework nếu DT thật sự khai "mclk"
	 * (giống hệt cách nv_imx219.c làm trên board P3768). Trên board này,
	 * "cam_mclk1"/"cam_mclk2" không tồn tại như clock qua BPMP — MCLK là
	 * oscillator cố định luôn bật ở tầng phần cứng, không cần software
	 * bật/tắt. Không còn fallback cứng "cam_mclk1" như bản trước (bug đã
	 * sửa — xem ghi chú ở đầu hàm).
	 */
	mclk_name = pdata->mclk_name;
	if (mclk_name) {
		pw->mclk = devm_clk_get(dev, mclk_name);
		if (IS_ERR(pw->mclk)) {
			dev_err(dev, "unable to get clock %s\n", mclk_name);
			return PTR_ERR(pw->mclk);
		}

		parentclk_name = pdata->parentclk_name;
		if (parentclk_name) {
			parent = devm_clk_get(dev, parentclk_name);
			if (IS_ERR(parent)) {
				dev_err(dev, "unable to get parent clock %s",
					parentclk_name);
			} else {
				ret = clk_set_parent(pw->mclk, parent);
				if (ret < 0)
					dev_dbg(dev, "%s failed to set parent clock %d\n",
						__func__, ret);
			}
		}
	}

	/* GIỮ NGUYÊN bản gốc: cả 3 regulator, non-fatal per-rail (khớp
	 * ov9281_power_on() đã port, coi avdd/dvdd/iovdd đều optional) —
	 * KHÁC OV5693 dùng err |= (fatal nếu thiếu avdd/iovdd).
	 */
	if (pdata->regulators.avdd) {
		err = camera_common_regulator_get(dev,
			&pw->avdd, pdata->regulators.avdd);
		if (err)
			dev_err(dev, "unable to get regulator %s, err = %d\n",
				pdata->regulators.avdd, err);
	}

	if (pdata->regulators.dvdd) {
		err = camera_common_regulator_get(dev,
			&pw->dvdd, pdata->regulators.dvdd);
		if (err)
			dev_err(dev, "unable to get regulator %s, err = %d\n",
				pdata->regulators.dvdd, err);
	}

	if (pdata->regulators.iovdd) {
		err = camera_common_regulator_get(dev,
			&pw->iovdd, pdata->regulators.iovdd);
		if (err)
			dev_err(dev, "unable to get regulator %s, err = %d\n",
				pdata->regulators.iovdd, err);
	}

	pw->reset_gpio = pdata->reset_gpio;
	if (gpio_is_valid(pw->reset_gpio)) {
		ret = gpio_request(pw->reset_gpio, "cam_reset_gpio");
		if (ret < 0)
			dev_dbg(dev, "%s can't request reset_gpio %d\n",
				__func__, ret);
		gpio_direction_output(pw->reset_gpio, 0);
	}

	pw->state = SWITCH_OFF;
	return 0;
}

/*
 * ===================== set_fmt / get_fmt =====================
 * ĐÃ XÁC NHẬN (đọc code gốc thật): 2 hàm này chỉ là wrapper rỗng gọi
 * camera_common_try_fmt/s_fmt/g_fmt — KHÔNG chứa logic Bayer/RAW riêng nào.
 * -> XÓA HOÀN TOÀN khi port, không mất logic gì. Framework tegracam v2.0
 * tự quản lý format qua mode table (ov9281_mode_tbls.h) + OV9281_DEFAULT_DATAFMT
 * (đã sửa ở trên, từ SBGGR10 Bayer -> Y10 mono).
 *
 * (Không có code ở đây — đã xóa, đúng như dự định.)
 */
/* =====================================================================
 * ov9281_of_match — dời lên sớm hơn so với vị trí bản gốc (gần probe(),
 * dòng gốc 1159), giống OV5693 (dòng 371, đặt trước parse_dt) — vì
 * ov9281_parse_dt() bên dưới cần gọi of_match_device(ov9281_of_match, ...).
 * Bản gốc không cần đặt sớm vì framework v1 gọi parse_dt() trực tiếp trong
 * probe(), không qua of_match_device() để chọn ops.
 * ===================================================================== */
static const struct of_device_id ov9281_of_match[] = {
	{ .compatible = "ovti,ov9281", },
	{ },
};
MODULE_DEVICE_TABLE(of, ov9281_of_match);

/* =====================================================================
 * ov9281_parse_dt — port từ dòng gốc 1068, khuôn ov5693_parse_dt() (dòng 778).
 *
 * KHÁC BẢN GỐC (đã giải thích với người dùng, xác nhận trước khi viết):
 *  - Chữ ký đổi hoàn toàn: (struct i2c_client*, struct ov9281*) -> int
 *    thành (struct tegracam_device *tc_dev) -> struct camera_common_pdata*.
 *  - "mclk": dùng camera_common_parse_clocks() thay vì of_property_read_string
 *    thẳng — đã đọc code hàm này (camera_common.c:225), nó vẫn thử đúng
 *    of_property_read_string(np, "mclk", ...) trước tiên (tương thích ngược
 *    100% với property DT gốc), chỉ thêm fallback "clock-names"/"mclk-index"
 *    nếu không thấy — nâng cấp an toàn theo đúng khuôn OV5693/kernel 6.8.
 *  - "pwdn-gpios"/"reset-gpios": giữ tên property gốc, thêm xử lý
 *    -EPROBE_DEFER (framework mới yêu cầu, bản gốc không có).
 *  - "avdd-reg"/"dvdd-reg"/"iovdd-reg": giữ CẢ 3 (khác OV5693 chỉ có 2, vì
 *    OV9281 cần dvdd riêng theo datasheet mục 2.5) — camera_common_regulators
 *    có sẵn cả 3 field. Giữ non-fatal dev_warn như bản gốc, khớp với
 *    ov9281_power_on() đã port (coi avdd/dvdd/iovdd đều optional).
 *  - "mirror"/"flip": giữ tên property gốc, nhưng ghi vào
 *    board_priv_pdata->h_mirror/v_flip (field chuẩn có sẵn) thay vì priv
 *    riêng — xem giải thích trong struct ov9281 phía trên.
 *  - KHÔNG đọc "fsync"/"cam-sid-gpios"/"mcu-boot-gpios"/"mcu-reset-gpios" ở
 *    đây — xem TODO trong struct ov9281 phía trên (priv chưa tồn tại lúc
 *    hàm này chạy, cần đọc lại ở board_setup()/probe() sau).
 * ===================================================================== */
static struct camera_common_pdata *ov9281_parse_dt(struct tegracam_device
							*tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct device_node *node = dev->of_node;
	struct camera_common_pdata *board_priv_pdata;
	const struct of_device_id *match;
	int gpio;
	int err;
	struct camera_common_pdata *ret = NULL;

	if (!node)
		return NULL;

	match = of_match_device(ov9281_of_match, dev);
	if (!match) {
		dev_err(dev, "Failed to find matching dt id\n");
		return NULL;
	}

	board_priv_pdata = devm_kzalloc(dev,
			   sizeof(*board_priv_pdata), GFP_KERNEL);
	if (!board_priv_pdata)
		return NULL;

	/* KHÔNG gọi camera_common_parse_clocks() — khớp đúng cách
	 * imx219_parse_dt() làm (không hề gọi hàm này). MCLK trên board P3768
	 * là oscillator cố định luôn bật ở tầng phần cứng, không cần/không nên
	 * để phần mềm quản lý qua clock framework (xem ghi chú đầy đủ trong
	 * tegra234-camera-ov9281-dual.dtsi tại node ov9281_a@60 — property
	 * "mclk"="extperiph1"/"extperiph2" trước đây chỉ để né lỗi -ENODATA ở
	 * đây, không có bằng chứng nó lái chân MCLK vật lý, và enable/disable
	 * nó không ảnh hưởng gì tới hành vi sensor thật khi đã test). Do đó
	 * board_priv_pdata->mclk_name giữ nguyên NULL (kzalloc), khớp guard
	 * `if (mclk_name)` đã có sẵn trong ov9281_power_get() và
	 * `if (pdata->mclk_name)` trong ov9281_board_setup(). */

	gpio = of_get_named_gpio(node, "pwdn-gpios", 0);
	if (gpio < 0) {
		if (gpio == -EPROBE_DEFER) {
			ret = ERR_PTR(-EPROBE_DEFER);
			goto error;
		}
		dev_dbg(dev, "pwdn gpios not in DT\n");
		gpio = 0;
	}
	board_priv_pdata->pwdn_gpio = (unsigned int)gpio;

	gpio = of_get_named_gpio(node, "reset-gpios", 0);
	if (gpio < 0) {
		if (gpio == -EPROBE_DEFER) {
			ret = ERR_PTR(-EPROBE_DEFER);
			goto error;
		}
		dev_dbg(dev, "reset gpios not in DT\n");
		gpio = 0;
	}
	board_priv_pdata->reset_gpio = (unsigned int)gpio;

	err = of_property_read_string(node, "avdd-reg",
			&board_priv_pdata->regulators.avdd);
	if (err)
		dev_warn(dev, "avdd-reg not in DT\n");

	err = of_property_read_string(node, "dvdd-reg",
			&board_priv_pdata->regulators.dvdd);
	if (err)
		dev_warn(dev, "dvdd-reg not in DT\n");

	err = of_property_read_string(node, "iovdd-reg",
			&board_priv_pdata->regulators.iovdd);
	if (err)
		dev_warn(dev, "iovdd-reg not in DT\n");

	board_priv_pdata->h_mirror = of_property_read_bool(node, "mirror");
	board_priv_pdata->v_flip = of_property_read_bool(node, "flip");

	return board_priv_pdata;

error:
	return ret;
}

/* =====================================================================
 * ov9281_set_mode — hàm MỚI (không có trong bản gốc, bắt buộc về kiến trúc
 * tegracam v2.0). Bản gốc gộp việc ghi "mode table chính" + 2 bảng fsync vào
 * chung ov9281_s_stream(enable=1) (dòng gốc 690-714) — tegracam v2.0 tách
 * riêng: framework gọi set_mode() khi đổi resolution/mode, start_streaming()
 * chỉ còn lo bật bit streaming. Không tách hàm này thì set_mode/fsync sẽ
 * không bao giờ được cấu hình.
 *
 * Nội dung/giá trị giữ NGUYÊN bản gốc, chỉ đổi:
 *  - s_data->mode -> s_data->mode_prop_idx (đúng field tegracam v2.0 dùng để
 *    chỉ mode hiện tại — khớp cách ov9281_set_frame_rate() đã port dùng).
 * ===================================================================== */
static int ov9281_set_mode(struct tegracam_device *tc_dev)
{
	struct ov9281 *priv = (struct ov9281 *)tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = tc_dev->s_data;
	struct device *dev = tc_dev->dev;
	int err;

	if (s_data->mode_prop_idx < 0)
		return -EINVAL;

	dev_dbg(dev, "%s: write mode table %d\n", __func__,
		s_data->mode_prop_idx);
	err = ov9281_write_table(priv, ov9281_mode_table[s_data->mode_prop_idx]);
	if (err)
		return err;

	if (priv->fsync < 0 || priv->fsync > OV9281_FSYNC_SLAVE)
		return -EINVAL;

	if (ov9281_fsync_table[priv->fsync]) {
		dev_dbg(dev, "%s: write fsync table %d\n", __func__,
			priv->fsync);
		err = ov9281_write_table(priv, ov9281_fsync_table[priv->fsync]);
		if (err)
			return err;
	}

	if ((priv->fsync == OV9281_FSYNC_SLAVE) &&
	    ov9281_fsync_slave_mode_table[s_data->mode_prop_idx]) {
		dev_dbg(dev, "%s: write fsync slave mode table %d\n",
			__func__, s_data->mode_prop_idx);
		err = ov9281_write_table(priv,
			ov9281_fsync_slave_mode_table[s_data->mode_prop_idx]);
		if (err)
			return err;
	}

	return 0;
}

/* =====================================================================
 * ov9281_start_streaming — port từ nhánh enable=1 của ov9281_s_stream() gốc
 * (dòng 685-793), khuôn ov5693_start_streaming().
 *
 * Thay đổi so với bản gốc:
 *  1. fsync: giữ nguyên priv->fsync, dùng bình thường (nơi gán giá trị cho
 *     field này để ở Phần 5.3 board_setup()/probe(), chưa xử lý ở đây).
 *  2. mirror/flip: priv->mirror/priv->flip -> s_data->pdata->h_mirror/v_flip
 *     (đã port ở ov9281_parse_dt() — xem giải thích trong struct ov9281).
 *  3. Nhánh override_enable (đọc lại gain/frame_length/coarse_time qua
 *     ctrl_handler kiểu v1 cũ) — BỎ HẲN theo yêu cầu, không port (tính năng
 *     debug, không cần cho bring-up cơ bản; framework v2.0 cũng không có
 *     khái niệm override_enable kiểu này trong start_streaming của OV5693).
 *  4. priv->frame_period_ms (mili-giây) -> KHÔNG dùng thẳng priv->frame_length
 *     (thanh ghi VTS thô, đơn vị dòng quét, không phải ms — 2 field khác ý
 *     nghĩa dù Phần 0 đã đổi tên). Tính lại đúng thời gian thật bằng công thức
 *     frame_time = frame_length(dòng) * line_length(pixel) / pixel_clock(Hz),
 *     cùng cấu trúc ov5693_stop_streaming() dùng (đã đọc code thật, nv_ov5693.c
 *     dòng 951-954) nhưng dùng mode->signal_properties.pixel_clock.val (đơn vị
 *     Hz thật lấy từ mode table, không phải hằng số MHz cứng như OV5693) nên
 *     nhân thêm 1000 để ra mili-giây cho đúng đơn vị msleep_range() cần.
 *  5. Khối #ifdef TPG (test pattern) — giữ nguyên, giữ trong #ifdef TPG.
 *
 * Thứ tự giữ đúng bản gốc: mirror/flip -> TPG -> ghi bảng START_STREAM ->
 * delay fsync-slave (không đổi theo thứ tự ov5693_start_streaming vì đây là
 * trình tự phần cứng OV9281 gốc, ưu tiên giữ nguyên logic/nội dung OV9281).
 * ===================================================================== */
static int ov9281_start_streaming(struct tegracam_device *tc_dev)
{
	struct ov9281 *priv = (struct ov9281 *)tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	const struct sensor_mode_properties *mode =
		&s_data->sensor_props.sensor_modes[s_data->mode_prop_idx];
	int err;

	/*
	 * Handle mirror and flip.
	 * Vertical and horizontal binning are in the same registers, so
	 * need to take frame resolution into account (to avoid a register
	 * read).
	 */
	if (pdata->h_mirror) {
		if (s_data->frmfmt->size.width > (OV9281_MAX_WIDTH / 2))
			ov9281_write_reg(s_data, OV9281_TIMING_FORMAT2,
					 OV9281_TIMING_FORMAT2_MIRROR);
		else
			ov9281_write_reg(s_data, OV9281_TIMING_FORMAT2,
					 OV9281_TIMING_FORMAT2_HBIN |
					 OV9281_TIMING_FORMAT2_MIRROR);
	}

	if (pdata->v_flip) {
		if (s_data->frmfmt->size.height > (OV9281_MAX_HEIGHT / 2))
			ov9281_write_reg(s_data, OV9281_TIMING_FORMAT1,
					 OV9281_TIMING_FORMAT1_FLIP);
		else
			ov9281_write_reg(s_data, OV9281_TIMING_FORMAT1,
					 OV9281_TIMING_FORMAT1_VBIN |
					 OV9281_TIMING_FORMAT1_FLIP);
	}

#ifdef TPG
	err = ov9281_write_reg(s_data, OV9281_PRE_CTRL00_ADDR,
			       OV9281_PRE_CTRL00_TEST_PATTERN_EN);
	if (err)
		dev_warn(dev, "%s: error enabling TPG\n", __func__);
#endif

	mutex_lock(&priv->streaming_lock);
	err = ov9281_write_table(priv,
		ov9281_mode_table[OV9281_MODE_START_STREAM]);
	if (err) {
		mutex_unlock(&priv->streaming_lock);
		goto exit;
	}
	priv->streaming = true;
	mutex_unlock(&priv->streaming_lock);

	/*
	 * If the sensor is in fsync slave mode, and is in the middle of
	 * sending a frame when it gets a strobe on the fsin pin, it may
	 * prematurely end the frame, resulting in a short frame on our
	 * camera host. So, after starting streaming, we assume fsync
	 * master has already been told to start streaming, and we wait some
	 * amount of time in order to skip the possible short frame.
	 * (Giữ nguyên ý nghĩa/comment bản gốc, dòng 780-789 — chỉ đổi cách
	 * tính ra mili-giây thật thay vì dùng priv->frame_length thô.)
	 */
	if (priv->fsync == OV9281_FSYNC_SLAVE) {
		u32 frame_time_ms = (u32)(((u64)priv->frame_length *
			mode->image_properties.line_length * 1000) /
			mode->signal_properties.pixel_clock.val);

		msleep_range(frame_time_ms + 10);
	}

	return 0;

exit:
	dev_err(dev, "%s: error starting stream\n", __func__);
	return err;
}

/* =====================================================================
 * ov9281_stop_streaming — port từ nhánh enable=0 của ov9281_s_stream() gốc
 * (dòng 681-684).
 *
 * KHÔNG thêm delay chờ hết frame kiểu ov5693_stop_streaming() (frame_time
 * tính từ frame_length/line_length/pixel_clock rồi usleep_range) — dù đó là
 * pattern OV5693 dùng làm khuôn cấu trúc. Lý do: datasheet OV9281 mục 2.5.2
 * (đã trích trong OV9281-Jetson-Project-Plan.md) ghi rõ sensor TỰ đợi tới
 * cuối frame MIPI trước khi vào standby nếu lệnh "exit streaming" đến giữa
 * lúc đang xuất frame — không cần driver tự thêm delay thủ công. Đây là
 * trường hợp nội dung/vật lý OV9281 khác OV5693, giữ đúng theo bản gốc
 * (nguyên tắc dự án #1: nội dung/logic lấy từ nv_ov9281.c, không áp máy móc
 * mọi chi tiết cấu trúc OV5693).
 *
 * Thêm mutex streaming_lock bọc quanh (bản gốc không có, vì v1 không có khái
 * niệm streaming_lock) — khớp pattern đã dùng ở ov9281_read_otp()/
 * ov9281_start_streaming() phía trên.
 * ===================================================================== */
static int ov9281_stop_streaming(struct tegracam_device *tc_dev)
{
	struct ov9281 *priv = (struct ov9281 *)tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = tc_dev->s_data;
	struct device *dev = s_data->dev;
	int err;

	mutex_lock(&priv->streaming_lock);
	err = ov9281_write_table(priv,
		ov9281_mode_table[OV9281_MODE_STOP_STREAM]);
	if (err) {
		mutex_unlock(&priv->streaming_lock);
		goto exit;
	}
	priv->streaming = false;
	mutex_unlock(&priv->streaming_lock);

	return 0;

exit:
	dev_err(dev, "%s: error stopping stream\n", __func__);
	return err;
}

/* =====================================================================
 * ov9281_common_ops — struct camera_common_sensor_ops (ĐÃ XÁC NHẬN đúng tên,
 * OV5693 dùng cùng tên struct này, không đổi thành "tegracam_sensor_ops" như
 * TODO trước đó nghi ngờ — xem nv_ov5693.c dòng 963). .set_mode/
 * .start_streaming/.stop_streaming/.power_get/.power_put đã wire đủ vào các
 * hàm đã port. Struct này giờ đã đầy đủ so với danh sách field OV5693 dùng.
 *
 * .numfrmfmts/.frmfmt_table — THÊM (Phần 5.3): đã đọc code thật
 * tegracam_core.c:145-146, tegracam_device_register() lấy s_data->frmfmt/
 * numfmts từ đúng 2 field này, không set thủ công trong probe() như bản gốc
 * (common_data->frmfmt = ov9281_frmfmt, dòng gốc 1208) nữa. ov9281_frmfmt[]
 * đã có sẵn trong ov9281_mode_tbls.h (include từ đầu file), không cần viết
 * mới — chỉ cần wire vào đây.
 * ===================================================================== */
static struct camera_common_sensor_ops ov9281_common_ops = {
	.numfrmfmts = ARRAY_SIZE(ov9281_frmfmt),
	.frmfmt_table = ov9281_frmfmt,
	.power_on = ov9281_power_on,
	.power_off = ov9281_power_off,
	.write_reg = ov9281_write_reg,
	.read_reg = ov9281_read_reg,
	.parse_dt = ov9281_parse_dt,
	.power_get = ov9281_power_get,
	.power_put = ov9281_power_put,
	.set_mode = ov9281_set_mode,
	.start_streaming = ov9281_start_streaming,
	.stop_streaming = ov9281_stop_streaming,
};

/* =====================================================================
 * ov9281_set_group_hold — đã port lại (bản sửa lần 2, sau khi có code thật OV5693)
 *
 * Sửa so với bản gửi lần trước:
 *  - priv = tc_dev->priv  (KHÔNG dùng tegracam_get_privdata — đã xác nhận sai)
 *  - Thêm camera_common_i2c_aggregate() bọc quanh group hold (tối ưu I2C,
 *    cần thêm field i2c_dev vào struct — đã có sẵn trong struct đã port trước)
 *  - GIỮ hằng số OV9281_GROUP_HOLD_START/END/LAUNCH_VBLANK của bản gốc
 *    (không copy giá trị "val"/"0x11"/"0x61" ghi thẳng kiểu OV5693 — nghi ngờ
 *    đó là cách viết tắt riêng của OV5693, có thể gây nhầm lẫn ý nghĩa register
 *    nếu áp trực tiếp cho OV9281 mà không đối chiếu lại datasheet)
 * ===================================================================== */
static int ov9281_set_group_hold(struct tegracam_device *tc_dev, bool val)
{
	struct ov9281 *priv = tc_dev->priv;
	int gh_prev = switch_ctrl_qmenu[priv->group_hold_prev];
	struct device *dev = tc_dev->dev;
	int err;

	priv->group_hold_en = val;

	if (priv->group_hold_en == true && gh_prev == SWITCH_OFF) {
		camera_common_i2c_aggregate(&priv->i2c_dev, true);
		/* group hold start */
		err = ov9281_write_reg(priv->s_data, OV9281_GROUP_HOLD_ADDR,
				       (OV9281_GROUP_HOLD_START |
					OV9281_GROUP_HOLD_BANK_0));
		if (err)
			goto fail;
		priv->group_hold_prev = 1;
		dev_dbg(dev, "%s: enter group hold\n", __func__);
	} else if (priv->group_hold_en == false && gh_prev == SWITCH_ON) {
		/* group hold end */
		err = ov9281_write_reg(priv->s_data, OV9281_GROUP_HOLD_ADDR,
				       (OV9281_GROUP_HOLD_END |
					OV9281_GROUP_HOLD_BANK_0));
		if (err)
			goto fail;

		/* quick launch */
		err = ov9281_write_reg(priv->s_data, OV9281_GROUP_HOLD_ADDR,
				       (OV9281_GROUP_HOLD_LAUNCH_VBLANK |
					OV9281_GROUP_HOLD_BANK_0));
		if (err)
			goto fail;

		camera_common_i2c_aggregate(&priv->i2c_dev, false);
		priv->group_hold_prev = 0;
		dev_dbg(dev, "%s: leave group hold\n", __func__);
	}

	return 0;

fail:
	dev_dbg(dev, "%s: Group hold control error\n", __func__);
	return err;
}

/* =====================================================================
 * ov9281_set_gain — đã port lại (bản sửa lần 3)
 *
 * SỬA so với bản trước: khôi phục ghi OV9281_GAIN_SHIFT_ADDR (0x3507) cùng
 * lúc với GAIN_HIGH (0x3508)/GAIN_LOW (0x3509), đúng như bản gốc v1
 * (R35.6.5 nv_ov9281.c, ov9281_set_gain(): regs[0]=0x3507/0x03, regs[1]=
 * 0x3508, regs[2]=0x3509, regs[3]=TABLE_END, ghi qua ov9281_write_table()).
 *
 * Lý do: thực nghiệm dmesg cho thấy trong chuỗi 3 lệnh regmap_write() liên
 * tiếp không delay (0x3208 group-hold -> 0x3508 -> 0x3509), 0x3208 và 0x3508
 * PASS nhưng 0x3509 luôn FAIL (-121/EREMOTEIO) — tức không phải mất kết nối
 * I2C hoàn toàn, mà lỗi giữa chừng một chuỗi giao dịch. Bản port trước đã bỏ
 * hẳn dòng ghi 0x3507 (coi nó chỉ cần set 1 lần lúc init mode table) — quay
 * lại ghi đủ 3 register mỗi lần set_gain, qua write_table() (có
 * OV9281_TABLE_WAIT_MS giữa các lệnh, khác hẳn write_reg() rời rạc không
 * delay) để khớp đúng hành vi bản gốc.
 * ===================================================================== */
static inline void ov9281_get_gain_reg(ov9281_reg *regs, u16 gain)
{
	regs->addr = OV9281_GAIN_SHIFT_ADDR;
	regs->val = 0x03;
	(regs + 1)->addr = OV9281_GAIN_HIGH_ADDR;
	(regs + 1)->val = (gain >> 8) & 0xff;
	(regs + 2)->addr = OV9281_GAIN_LOW_ADDR;
	(regs + 2)->val = gain & 0xff;
	(regs + 3)->addr = OV9281_TABLE_END;
	(regs + 3)->val = 0;
}

static int ov9281_set_gain(struct tegracam_device *tc_dev, s64 val)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct ov9281 *priv = (struct ov9281 *)tc_dev->priv;
	struct device *dev = tc_dev->dev;
	const struct sensor_mode_properties *mode =
		&s_data->sensor_props.sensor_modes[s_data->mode_prop_idx];
	ov9281_reg reg_list[4];
	int err;
	u16 gain;

	if (!priv->group_hold_prev)
		ov9281_set_group_hold(tc_dev, 1);

	/* translate value — công thức xác nhận đúng cho OV9281:
	 * DEFAULT_GAIN=0x0010 (=16 decimal) tương ứng "1.0x real gain"
	 */
	gain = (u16)(((val * 16) +
		     (mode->control_properties.gain_factor / 2)) /
		    mode->control_properties.gain_factor);

	if (gain < OV9281_MIN_GAIN)
		gain = OV9281_MIN_GAIN;
	else if (gain > OV9281_MAX_GAIN)
		gain = OV9281_MAX_GAIN;

	ov9281_get_gain_reg(reg_list, gain);
	dev_dbg(dev, "%s: gain %d val: %lld\n", __func__, gain, val);

	err = ov9281_write_table(priv, reg_list);
	if (err)
		goto fail;

	return 0;

fail:
	dev_dbg(dev, "%s: GAIN control error\n", __func__);
	return err;
}

/* =====================================================================
 * ov9281_set_frame_rate — port từ ov9281_set_frame_length (đổi tên theo
 * khuôn OV5693: "frame_length" (giá trị register thô) -> "frame_rate" (đơn vị
 * framework, thường là fps × factor)
 *
 * Thay đổi so với bản gốc:
 *  - Tham số: (priv, s32 val register thô) -> (tc_dev, s64 val đơn vị framework)
 *  - THÊM công thức quy đổi val -> frame_length (giống hệt cấu trúc OV5693,
 *    dùng pixel_clock/line_length/framerate_factor từ mode table)
 *  - priv->frame_period_ms -> priv->frame_length (theo struct đã đổi tên trước)
 *    lưu THẲNG giá trị frame_length (register thô), không tính period_ms nữa
 *    — khớp cách OV5693 làm (framework tự suy fps ngược lại khi cần hiển thị)
 *  - GIỮ NGUYÊN: nhánh fsync == OV9281_FSYNC_SLAVE (tính năng riêng OV9281,
 *    OV5693 không có) — vẫn ghi thêm 2 register RST_FSIN khi ở chế độ slave
 *
 * ⚠️ CẦN THÊM MỚI (chưa làm, thuộc Giai đoạn 2/3): mode table OV9281 phải khai
 * sensor_mode_properties với pixel_clock.val, line_length, framerate_factor —
 * các field này CHƯA có trong ov9281_mode_tbls.h gốc (vốn chỉ dùng struct
 * camera_common_frmfmt kiểu cũ), phải viết thêm khi chuyển mode table sang v2.0.
 * ===================================================================== */
static int ov9281_set_frame_rate(struct tegracam_device *tc_dev, s64 val)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct device *dev = tc_dev->dev;
	struct ov9281 *priv = tc_dev->priv;
	const struct sensor_mode_properties *mode =
		&s_data->sensor_props.sensor_modes[s_data->mode_prop_idx];
	ov9281_reg reg_list[3];
	int err;
	u16 frame_length;
	int i, reg_count = 2;

	if (!priv->group_hold_prev)
		ov9281_set_group_hold(tc_dev, 1);

	frame_length = (u16)(mode->signal_properties.pixel_clock.val *
		mode->control_properties.framerate_factor /
		mode->image_properties.line_length / val);

	dev_dbg(dev, "%s: frame_length: %d\n", __func__, frame_length);

	reg_list[0].addr = OV9281_TIMING_VTS_HIGH_ADDR;
	reg_list[0].val = (frame_length >> 8) & 0xff;
	reg_list[1].addr = OV9281_TIMING_VTS_LOW_ADDR;
	reg_list[1].val = (frame_length) & 0xff;

	/* GIỮ NGUYÊN: tính năng riêng OV9281 — fsync slave cần đồng bộ thêm
	 * 2 register reset FSIN (frame_length - 4, theo comment gốc datasheet-adjacent) */
	if (priv->fsync == OV9281_FSYNC_SLAVE) {
		ov9281_reg fsin_regs[2];

		fsin_regs[0].addr = OV9281_TIMING_RST_FSIN_HIGH_ADDR;
		fsin_regs[0].val = ((frame_length - 4) >> 8) & 0xff;
		fsin_regs[1].addr = OV9281_TIMING_RST_FSIN_LOW_ADDR;
		fsin_regs[1].val = (frame_length - 4) & 0xff;

		for (i = 0; i < 2; i++) {
			err = ov9281_write_reg(s_data, fsin_regs[i].addr,
					       fsin_regs[i].val);
			if (err)
				goto fail;
		}
	}

	for (i = 0; i < reg_count; i++) {
		err = ov9281_write_reg(s_data, reg_list[i].addr,
				       reg_list[i].val);
		if (err)
			goto fail;
	}

	priv->frame_length = frame_length;

	return 0;

fail:
	dev_dbg(dev, "%s: FRAME_LENGTH control error\n", __func__);
	return err;
}

/* =====================================================================
 * ov9281_set_exposure — port từ ov9281_set_coarse_time (đổi tên theo khuôn OV5693)
 *
 * Thay đổi so với bản gốc:
 *  - Tham số: (priv, s32 val register thô) -> (tc_dev, s64 val đơn vị framework)
 *  - THÊM công thức quy đổi (giống cấu trúc OV5693)
 *  - THÊM clamp min/max (bản gốc OV9281 KHÔNG clamp gì cả — chỉ ép kiểu thẳng,
 *    đây là chỗ bản gốc có thể có rủi ro exposure vượt quá frame_length; OV5693
 *    clamp max = frame_length - MAX_COARSE_DIFF(6). OV9281 dùng thanh ghi 20-bit
 *    khác cấu trúc OV5693 (16-bit) nên KHÔNG rõ có cần "diff" tương tự không —
 *    tạm dùng OV9281_MIN/MAX_EXPOSURE_COARSE có sẵn, và thêm so sánh với
 *    priv->frame_length cho an toàn. CẦN XÁC NHẬN lại với datasheet/behavior
 *    thực tế khi test, có thể không cần clamp theo frame_length như OV5693.)
 *
 * GIỮ NGUYÊN: cấu trúc thanh ghi 3-byte kiểu byte-split đơn giản (bit 16-8-0),
 * khác OV5693 dùng bit-shift lệch 12/4/0 — đây là khác biệt phần cứng thật giữa
 * 2 chip (OV9281 dùng thanh ghi exposure 20-bit chia đều theo byte, OV5693 dùng
 * layout khác) — KHÔNG được áp công thức bit-shift của OV5693 vào đây.
 * ===================================================================== */
static int ov9281_set_exposure(struct tegracam_device *tc_dev, s64 val)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct device *dev = tc_dev->dev;
	struct ov9281 *priv = tc_dev->priv;
	const struct sensor_mode_properties *mode =
		&s_data->sensor_props.sensor_modes[s_data->mode_prop_idx];
	ov9281_reg reg_list[3];
	int err;
	u32 coarse_time;
	int i;

	if (!priv->group_hold_prev)
		ov9281_set_group_hold(tc_dev, 1);

	coarse_time = (u32)(((mode->signal_properties.pixel_clock.val * val)
			/ mode->image_properties.line_length) /
			mode->control_properties.exposure_factor);

	if (coarse_time < OV9281_MIN_EXPOSURE_COARSE)
		coarse_time = OV9281_MIN_EXPOSURE_COARSE;
	else if (coarse_time > OV9281_MAX_EXPOSURE_COARSE)
		coarse_time = OV9281_MAX_EXPOSURE_COARSE;
	/* TODO: xác nhận có cần clamp thêm theo priv->frame_length không
	 * (giống OV5693 dùng max_coarse_time = frame_length - MAX_COARSE_DIFF) */

	dev_dbg(dev, "%s: coarse_time: %d\n", __func__, coarse_time);

	reg_list[0].addr = OV9281_EXPO_HIGH_ADDR;
	reg_list[0].val = (coarse_time >> 16) & 0xff;
	reg_list[1].addr = OV9281_EXPO_MID_ADDR;
	reg_list[1].val = (coarse_time >> 8) & 0xff;
	reg_list[2].addr = OV9281_EXPO_LOW_ADDR;
	reg_list[2].val = (coarse_time & 0xff);

	for (i = 0; i < 3; i++) {
		err = ov9281_write_reg(s_data, reg_list[i].addr,
				       reg_list[i].val);
		if (err)
			goto fail;
	}

	return 0;

fail:
	dev_dbg(dev, "%s: COARSE_TIME control error\n", __func__);
	return err;
}

/* =====================================================================
 * ov9281_read_otp — GIỮ NGUYÊN cơ chế đọc OTP gốc của OV9281 (khác hẳn
 * cách OV5693 dùng bank-select — đây là 2 chip có layout OTP khác nhau,
 * KHÔNG áp công thức OV5693 vào đây).
 *
 * Thay đổi so với bản gốc:
 *  - regmap lấy qua s_data->regmap (đã xóa priv->regmap ở Phần 0)
 *  - THÊM mutex streaming_lock bọc quanh việc bật/tắt streaming — theo pattern
 *    OV5693 (ov5693_read_otp_bank), quan trọng hơn với OV9281 vì dự án chạy
 *    2 camera đồng thời, cần tránh race condition khi đọc OTP lúc sensor
 *    khác đang stream.
 * ===================================================================== */
static int ov9281_read_otp(struct ov9281 *priv, u8 *buf, u16 addr, int size)
{
	struct camera_common_data *s_data = priv->s_data;
	int i;
	int err;

	mutex_lock(&priv->streaming_lock);
	err = ov9281_write_reg(s_data, OV9281_SC_MODE_SELECT_ADDR,
			       OV9281_SC_MODE_SELECT_STREAMING);
	if (err) {
		mutex_unlock(&priv->streaming_lock);
		return err;
	}
	priv->streaming = true;
	mutex_unlock(&priv->streaming_lock);

	for (i = 0; i < size; i++) {
		err = ov9281_write_reg(s_data, addr + i, 0x00);
		if (err)
			return err;
	}

	err = ov9281_write_reg(s_data, OV9281_OTP_LOAD_CTRL_ADDR,
			       OV9281_OTP_LOAD_CTRL_OTP_RD);
	if (err)
		return err;

	msleep(20);

	err = regmap_bulk_read(s_data->regmap, addr, buf, size);
	if (err)
		return err;

	mutex_lock(&priv->streaming_lock);
	err = ov9281_write_reg(s_data, OV9281_SC_MODE_SELECT_ADDR, 0x00);
	if (!err)
		priv->streaming = false;
	mutex_unlock(&priv->streaming_lock);

	return err;
}

/* =====================================================================
 * ov9281_otp_setup / ov9281_fuse_id_setup — đơn giản hơn OV5693 (không có
 * khái niệm "bank" — OV9281 chỉ có 1 buffer OTP 32 byte và 1 vùng fuse ID
 * 16 byte riêng, không cần vòng lặp OV5693_OTP_NUM_BANKS).
 *
 * Thay đổi: chỉ đọc dữ liệu thô vào priv->otp_buf/priv->fuse_id (buffer mới
 * cần THÊM vào struct — xem ghi chú cuối). KHÔNG tự gọi v4l2_ctrl_find nữa —
 * việc "nhét" vào ctrl string chuyển sang ov9281_fill_string_ctrl bên dưới.
 * ===================================================================== */
static int ov9281_otp_setup(struct ov9281 *priv)
{
	return ov9281_read_otp(priv, priv->otp_buf,
			       OV9281_OTP_BUFFER_ADDR,
			       OV9281_OTP_BUFFER_SIZE);
}

static int ov9281_fuse_id_setup(struct ov9281 *priv)
{
	return ov9281_read_otp(priv, priv->fuse_id,
			       OV9281_FUSE_ID_OTP_BUFFER_ADDR,
			       OV9281_FUSE_ID_OTP_BUFFER_SIZE);
}

/* =====================================================================
 * ov9281_fill_string_ctrl — hàm MỚI (không có trong bản gốc OV9281), copy
 * khuôn ov5693_fill_string_ctrl. Đăng ký vào tegracam_ctrl_ops.fill_string_ctrl
 * (cần thêm vào Phần 0.5) để framework tự gọi khi user đọc CID OTP/FUSE_ID.
 *
 * KHÁC OV5693: không có case TEGRA_CAMERA_CID_EEPROM_DATA vì OV9281 không có
 * EEPROM riêng (không thấy trong bản gốc — chip chỉ có OTP nội bộ).
 * ===================================================================== */
static int ov9281_fill_string_ctrl(struct tegracam_device *tc_dev,
				   struct v4l2_ctrl *ctrl)
{
	struct ov9281 *priv = tc_dev->priv;
	int i, ret, size;
	u8 *src;

	switch (ctrl->id) {
	case TEGRA_CAMERA_CID_OTP_DATA:
		size = OV9281_OTP_BUFFER_SIZE;
		src = priv->otp_buf;
		break;
	case TEGRA_CAMERA_CID_FUSE_ID:
		size = OV9281_FUSE_ID_OTP_BUFFER_SIZE;
		src = priv->fuse_id;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < size; i++) {
		ret = sprintf(&ctrl->p_new.p_char[i * 2], "%02x", src[i]);
		if (ret < 0)
			return -EINVAL;
	}

	ctrl->p_cur.p_char = ctrl->p_new.p_char;

	return 0;
}

/* =====================================================================
 * ov9281_ctrl_ops — Phần 0.5, khuôn ov5693_ctrl_ops (nv_ov5693.c dòng 1057).
 *
 * .string_ctrl_size: KHÁC cách hiểu ban đầu (không phải theo thứ tự khai
 * trong ctrl_cid_list) — đã đọc code thật tegracam_ctrls.c:200-219
 * (tegracam_get_string_ctrl_size()), field này được truy cập qua index CỐ
 * ĐỊNH theo CID: TEGRA_CAM_STRING_CTRL_EEPROM_INDEX=0, _FUSEID_INDEX=1,
 * _OTP_INDEX=2 (tegra-v4l2-camera.h:85-87). Dùng designated initializer cho
 * đúng, rõ ràng, tránh dựa vào thứ tự vị trí như OV5693 làm (OV5693 có đủ 3
 * loại nên viết positional `{A, B, C}` vẫn đúng — OV9281 không có EEPROM nên
 * PHẢI dùng designated initializer, nếu viết positional sẽ lệch index).
 * Không có .set_exposure_short (OV9281 không có tính năng HDR/short-exposure
 * — đã kiểm tra grep code gốc, không có V4L2_CID liên quan).
 * ===================================================================== */
static struct tegracam_ctrl_ops ov9281_ctrl_ops = {
	.numctrls = ARRAY_SIZE(ctrl_cid_list),
	.ctrl_cid_list = ctrl_cid_list,
	.string_ctrl_size = {
		[TEGRA_CAM_STRING_CTRL_FUSEID_INDEX] = OV9281_FUSE_ID_STR_SIZE,
		[TEGRA_CAM_STRING_CTRL_OTP_INDEX] = OV9281_OTP_STR_SIZE,
	},
	.set_gain = ov9281_set_gain,
	.set_exposure = ov9281_set_exposure,
	.set_frame_rate = ov9281_set_frame_rate,
	.set_group_hold = ov9281_set_group_hold,
	.fill_string_ctrl = ov9281_fill_string_ctrl,
};

/* =====================================================================
 * ov9281_parse_dt_priv — hàm MỚI (không có trong bản gốc, cũng không có
 * tương đương bên OV5693). Chứa đúng phần bị "treo" lại từ ov9281_parse_dt()
 * (Phần 5.1) — property riêng OV9281 không có chỗ trong camera_common_pdata
 * chuẩn, không đọc được trong parse_dt() vì priv chưa tồn tại lúc đó (xem
 * giải thích chi tiết trong struct ov9281 phía trên). Gọi hàm này trong
 * ov9281_probe() ngay sau khi priv vừa được gán vào tc_dev (tegracam_set_
 * privdata), TRƯỚC ov9281_board_setup() — vì board_setup() gọi power_on(),
 * mà power_on() cần priv->cam_sid_gpio (qua ov9281_i2c_addr_assign()).
 *
 * Nội dung 2 đoạn dưới GIỮ NGUYÊN y hệt bản gốc (dòng 1081-1088, 1108) —
 * chỉ đổi cách lấy device_node (qua priv->i2c_client thay vì tham số client
 * riêng, vì hàm không còn nhận i2c_client làm tham số nữa).
 * ===================================================================== */
static void ov9281_parse_dt_priv(struct ov9281 *priv)
{
	struct device_node *np = priv->i2c_client->dev.of_node;
	const char *fsync_str;
	int err;

	err = of_property_read_string(np, "fsync", &fsync_str);
	if (!err && fsync_str && (strcmp(fsync_str, "master") == 0))
		priv->fsync = OV9281_FSYNC_MASTER;
	else if (!err && fsync_str && (strcmp(fsync_str, "slave") == 0))
		priv->fsync = OV9281_FSYNC_SLAVE;
	else
		priv->fsync = OV9281_FSYNC_NONE;

	priv->cam_sid_gpio = of_get_named_gpio(np, "cam-sid-gpios", 0);

	/*
	 * mcu_boot_gpio/mcu_reset_gpio: đọc ở đây để nhất quán (cùng chỗ với
	 * fsync/cam_sid_gpio, cùng lý do priv chưa tồn tại lúc parse_dt()
	 * chạy), nhưng KHÔNG gpio_request() — xem giải thích trong
	 * ov9281_probe() tại chỗ dùng 2 field này.
	 */
	priv->mcu_boot_gpio = of_get_named_gpio(np, "mcu-boot-gpios", 0);
	priv->mcu_reset_gpio = of_get_named_gpio(np, "mcu-reset-gpios", 0);
}

/* =====================================================================
 * ov9281_verify_chip_id — Phần 5.2, port từ dòng gốc 1131. GIỮ NGUYÊN HOÀN
 * TOÀN nội dung/giá trị (0x300A/0x300B kỳ vọng 0x92/0x81 -> chip_id 0x9281)
 * — không phải callback framework nên không cần đổi chữ ký, chỉ là hàm nội
 * bộ gọi từ ov9281_board_setup().
 * ===================================================================== */
static int ov9281_verify_chip_id(struct ov9281 *priv)
{
	struct device *dev = &priv->i2c_client->dev;
	struct camera_common_data *s_data = priv->s_data;
	u8 chip_id_hi, chip_id_lo;
	u16 chip_id;
	int err;

	err = ov9281_read_reg(s_data, OV9281_SC_CHIP_ID_HIGH_ADDR, &chip_id_hi);
	if (err) {
		dev_err(dev, "Failed to read chip ID\n");
		return err;
	}
	err = ov9281_read_reg(s_data, OV9281_SC_CHIP_ID_LOW_ADDR, &chip_id_lo);
	if (err) {
		dev_err(dev, "Failed to read chip ID\n");
		return err;
	}

	chip_id = (chip_id_hi << 8) | chip_id_lo;
	if (chip_id != 0x9281) {
		dev_err(dev, "Read unknown chip ID 0x%04x\n", chip_id);
		return -EINVAL;
	}

	return 0;
}

/* =====================================================================
 * ov9281_board_setup — hàm MỚI, khuôn ov5693_board_setup() (nv_ov5693.c dòng
 * 1071). Đây là nơi thật sự chạy power_on() một lần lúc probe để xác minh
 * chip + đọc OTP/fuse ID, rồi power_off() lại (khớp chú thích gốc: các hàm
 * set_mode/start_streaming chỉ chạy khi framework thật sự yêu cầu stream).
 *
 * KHÁC ov5693_board_setup(): không có nhánh eeprom_device_init/read_eeprom
 * (OV9281 không có EEPROM riêng, đã xác nhận từ Phần 4 — chỉ có OTP nội bộ).
 * THÊM ov9281_verify_chip_id() ngay sau power_on(), TRƯỚC otp_setup() — đúng
 * vị trí bản gốc gọi trong probe() (dòng 1263, ngay sau camera_common_s_power
 * (true) — tương đương ov9281_power_on() ở đây).
 *
 * Giữ đúng pattern goto/label của OV5693: nếu mclk_enable lỗi thì return
 * thẳng (chưa bật gì cần tắt); nếu power_on lỗi thì cũng return thẳng (giữ
 * đúng hành vi OV5693, dù để lại mclk đang bật — đây là hành vi có chủ đích
 * trong code NVIDIA thật, không phải tôi tự thêm); mọi lỗi SAU power_on
 * (verify_chip_id/otp/fuse_id) đều goto error để đảm bảo power_off +
 * mclk_disable luôn chạy — kể cả khi mọi bước đều thành công (rơi tự nhiên
 * xuống nhãn error, vì đây vốn là chỗ dọn dẹp chung, không chỉ dùng khi lỗi).
 * ===================================================================== */
static int ov9281_board_setup(struct ov9281 *priv)
{
	struct camera_common_data *s_data = priv->s_data;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	int err = 0;

	dev_dbg(dev, "%s++\n", __func__);

	/* Guard theo pdata->mclk_name — khớp imx219_board_setup(). MCLK không
	 * khai trong DT nữa (xem .dtsi/parse_dt) nên nhánh này bình thường
	 * không chạy; giữ guard để không gọi clk_set_rate()/clk_prepare_enable()
	 * trên pw->mclk chưa từng được devm_clk_get() (NULL) nếu sau này có ai
	 * thêm lại "mclk" trong DT mà quên sửa chỗ này. */
	if (pdata->mclk_name) {
		err = camera_common_mclk_enable(s_data);
		if (err) {
			dev_err(dev, "Error %d turning on mclk\n", err);
			return err;
		}
	}

	err = ov9281_power_on(s_data);
	if (err) {
		dev_err(dev, "Error %d during power on sensor\n", err);
		return err;
	}

	err = ov9281_verify_chip_id(priv);
	if (err) {
		dev_err(dev, "Error %d verifying chip id\n", err);
		goto error;
	}

	err = ov9281_otp_setup(priv);
	if (err) {
		dev_err(dev, "Error %d reading otp data\n", err);
		goto error;
	}

	err = ov9281_fuse_id_setup(priv);
	if (err) {
		dev_err(dev, "Error %d reading fuse id data\n", err);
		goto error;
	}

	/* KHÔNG power_off()/mclk_disable() ở đây: nhánh thành công phải giữ
	 * nguyên nguồn/MCLK/reset đang bật, vì tegracam_v4l2subdev_register()
	 * gọi ngay sau đó (trong ov9281_probe()) sẽ chạy v4l2_ctrl_handler_setup()
	 * -> ghi thật các thanh ghi mặc định (0x3208/0x3508) trong khi sensor
	 * vẫn cần đang có XSHUTDOWN cao + MCLK chạy. Trước đây label error: bị
	 * rơi xuống vô điều kiện kể cả khi mọi bước ở trên đều pass, nên tắt
	 * MCLK + kéo reset xuống đúng ngay trước bước ghi đó -> EREMOTEIO.
	 */
	return 0;

error:
	ov9281_power_off(s_data);
	if (pdata->mclk_name)
		camera_common_mclk_disable(s_data);
	return err;
}

/* =====================================================================
 * ov9281_open / ov9281_subdev_internal_ops — boilerplate thuần framework,
 * không có logic OV9281 riêng nào (bản gốc v1 không có khái niệm này) —
 * copy y hệt ov5693_open()/ov5693_subdev_internal_ops (nv_ov5693.c dòng
 * 1136-1146).
 * ===================================================================== */
static int ov9281_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	dev_dbg(&client->dev, "%s:\n", __func__);
	return 0;
}

static const struct v4l2_subdev_internal_ops ov9281_subdev_internal_ops = {
	.open = ov9281_open,
};

/* =====================================================================
 * ov9281_probe — Phần 5.3, hàm tổng hợp. Khuôn ov5693_probe() (nv_ov5693.c
 * dòng 1149-1236), thay toàn bộ cách khởi tạo v4l2_subdev thủ công kiểu v1
 * bằng tegracam_device_register()/tegracam_v4l2subdev_register().
 *
 * Giữ nguyên guard "#if NV_I2C_DRIVER_STRUCT_PROBE_WITHOUT_I2C_DEVICE_ID_ARG"
 * (Linux 6.3 đổi chữ ký probe() i2c, bỏ tham số id) — đúng pattern migration
 * guide đã chỉ ra dùng LINUX_VERSION_CODE/conftest guard để hỗ trợ song song.
 *
 * Thứ tự gọi khớp đúng bản gốc + khuôn OV5693, chèn thêm ov9281_parse_dt_priv()
 * và đoạn reset MCU (Điểm 1-3 bạn yêu cầu) đúng vị trí tương ứng bản gốc:
 * ngay sau khi priv có sẵn (khớp vị trí gốc: ngay sau ov9281_power_get(),
 * dòng 1229-1244), TRƯỚC board_setup().
 * ===================================================================== */
#if defined(NV_I2C_DRIVER_STRUCT_PROBE_WITHOUT_I2C_DEVICE_ID_ARG) /* Linux 6.3 */
static int ov9281_probe(struct i2c_client *client)
#else
static int ov9281_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
#endif
{
	struct device *dev = &client->dev;
	struct device_node *node = client->dev.of_node;
	struct tegracam_device *tc_dev;
	struct ov9281 *priv;
	int err;
	const struct of_device_id *match;

	dev_info(dev, "probing v4l2 sensor.\n");

	match = of_match_device(ov9281_of_match, dev);
	if (!match) {
		dev_err(dev, "No device match found\n");
		return -ENODEV;
	}

	if (!IS_ENABLED(CONFIG_OF) || !node)
		return -EINVAL;

	priv = devm_kzalloc(dev, sizeof(struct ov9281), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	tc_dev = devm_kzalloc(dev, sizeof(struct tegracam_device), GFP_KERNEL);
	if (!tc_dev)
		return -ENOMEM;

	priv->i2c_client = tc_dev->client = client;
	tc_dev->dev = dev;
	strncpy(tc_dev->name, "ov9281", sizeof(tc_dev->name));
	tc_dev->dev_regmap_config = &ov9281_regmap_config;
	tc_dev->sensor_ops = &ov9281_common_ops;
	tc_dev->v4l2sd_internal_ops = &ov9281_subdev_internal_ops;
	tc_dev->tcctrl_ops = &ov9281_ctrl_ops;

	err = tegracam_device_register(tc_dev);
	if (err) {
		dev_err(dev, "tegra camera driver registration failed\n");
		return err;
	}

	priv->tc_dev = tc_dev;
	priv->s_data = tc_dev->s_data;
	priv->subdev = &tc_dev->s_data->subdev;
	tegracam_set_privdata(tc_dev, (void *)priv);
	mutex_init(&priv->streaming_lock);

	/* Điểm 1+2: fsync/cam_sid_gpio — đọc lại ở đây, priv vừa có xong. */
	ov9281_parse_dt_priv(priv);

	/*
	 * Điểm 3: "If our device tree node is given MCU GPIOs, then we are
	 * expected to reset the MCU." Giữ nguyên logic + comment gốc (dòng
	 * 1233-1244) — phần cứng thật (MCU phụ trợ trên board đối tác dùng
	 * OV9281), không phải code thừa. KHÔNG gpio_request() 2 GPIO này
	 * (khác reset_gpio ở Phần 2.2) — bản gốc cũng không request, nhiều
	 * khả năng 2 GPIO này thuộc sở hữu/được request bởi driver MCU/board
	 * khác, không phải chân riêng của OV9281; tự ý request ở đây có thể
	 * đụng độ với driver đó. Cần xác nhận thêm khi có board thật.
	 */
	if (gpio_is_valid(priv->mcu_boot_gpio) &&
	    gpio_is_valid(priv->mcu_reset_gpio)) {
		dev_info(dev, "Resetting MCU\n");
		gpio_set_value(priv->mcu_boot_gpio, 0);
		gpio_set_value(priv->mcu_reset_gpio, 0);
		msleep_range(1);
		gpio_set_value(priv->mcu_reset_gpio, 1);
	}

	err = ov9281_board_setup(priv);
	if (err) {
		tegracam_device_unregister(tc_dev);
		dev_err(dev, "board setup failed\n");
		return err;
	}

	err = tegracam_v4l2subdev_register(tc_dev, true);
	if (err) {
		dev_err(dev, "tegra camera subdev registration failed\n");
		return err;
	}

	dev_dbg(dev, "Detected OV9281 sensor\n");

	return 0;
}

/* =====================================================================
 * ov9281_remove — khuôn ov5693_remove() (nv_ov5693.c dòng 1238-1269), NHƯNG
 * ĐÃ SỬA 1 điểm khác bản gốc OV5693: KHÔNG gọi ov9281_power_put(tc_dev) trực
 * tiếp — đã đọc code thật tegracam_core.c:186-209
 * (tegracam_device_unregister()), hàm này TỰ ĐỘNG gọi
 * tc_dev->sensor_ops->power_put(tc_dev) bên trong (dòng 192). OV5693 gọi cả
 * 2 (power_put() trực tiếp RỒI mới unregister()) — gọi đôi, double
 * gpio_free()/regulator_put() trên cùng tài nguyên, rủi ro cảnh báo/lỗi
 * kernel thật. Sửa đúng lại cho OV9281: chỉ gọi tegracam_device_unregister().
 * Không có debugfs nên bỏ luôn ov9281_debugfs_remove().
 * ===================================================================== */
#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
static int
ov9281_remove(struct i2c_client *client)
#else
static void
ov9281_remove(struct i2c_client *client)
#endif
{
	struct ov9281 *priv;
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);

	if (!s_data)
#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
		return -EINVAL;
#else
		return;
#endif

	priv = (struct ov9281 *)s_data->priv;

	tegracam_v4l2subdev_unregister(priv->tc_dev);
	tegracam_device_unregister(priv->tc_dev);

	mutex_destroy(&priv->streaming_lock);
#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
	return 0;
#endif
}

static const struct i2c_device_id ov9281_id[] = {
	{ "ov9281", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, ov9281_id);

static struct i2c_driver ov9281_i2c_driver = {
	.driver = {
		.name = "ov9281",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ov9281_of_match),
	},
	.probe = ov9281_probe,
	.remove = ov9281_remove,
	.id_table = ov9281_id,
};

module_i2c_driver(ov9281_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for Omnivison OV9281");
MODULE_AUTHOR("NVIDIA Corporation");
MODULE_LICENSE("GPL v2");