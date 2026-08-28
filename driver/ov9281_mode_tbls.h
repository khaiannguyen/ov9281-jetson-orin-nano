/*
 * ov9281.c - ov9281 sensor driver
 *
 * Copyright (c) 2016-2017, NVIDIA CORPORATION, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <media/camera_common.h>

#ifndef __OV9281_I2C_TABLES__
#define __OV9281_I2C_TABLES__

#define OV9281_TABLE_WAIT_MS	0
#define OV9281_TABLE_END	1

#define ov9281_reg struct reg_8

enum {
	OV9281_MODE_1280X800,
	OV9281_MODE_1280X720,
	OV9281_MODE_640X400,
	OV9281_MODE_START_STREAM,
	OV9281_MODE_STOP_STREAM,
};

enum {
	OV9281_FSYNC_NONE,
	OV9281_FSYNC_MASTER,
	OV9281_FSYNC_SLAVE,
};

static const ov9281_reg ov9281_start[] = {
	{ 0x0100, 0x01 },
	{ OV9281_TABLE_END, 0x00 }
};

static const ov9281_reg ov9281_stop[] = {
	{ 0x0100, 0x00 },
	{ OV9281_TABLE_END, 0x00 }
};

static const ov9281_reg ov9281_fsync_master[] = {
	{ 0x3006, 0x02 }, /* fsin pin out */
	{ 0x3823, 0x00 },
	{ OV9281_TABLE_WAIT_MS, 66 },
	{ OV9281_TABLE_END, 0x00 }
};

static const ov9281_reg ov9281_fsync_slave[] = {
	{ 0x3006, 0x00 }, /* fsin pin in */
	{ 0x3007, 0x02 },
	{ 0x38b3, 0x07 },
	{ 0x3885, 0x07 },
	{ 0x382b, 0x5a },
	{ 0x3670, 0x68 },
	{ 0x3740, 0x01 },
	{ 0x3741, 0x00 },
	{ 0x3742, 0x08 },
	{ 0x3823, 0x30 }, /* ext_vs_en, r_init_man */
	{ 0x3824, 0x00 }, /* CS reset value on fsin */
	{ 0x3825, 0x08 },
	{ OV9281_TABLE_WAIT_MS, 66 },
	{ OV9281_TABLE_END, 0x00 }
};

static const ov9281_reg ov9281_mode_1280x800_26MhzMCLK[] = {
	/* PLL control */
	{ 0x0302, 0x32 },
	{ 0x030d, 0x50 },
	{ 0x030e, 0x02 },

	/* system control */
	{ 0x3001, 0x00 },
	{ 0x3004, 0x00 },
	{ 0x3005, 0x00 },
	{ 0x3006, 0x04 },
	{ 0x3011, 0x0a },
	{ 0x3013, 0x18 },
	{ 0x301c, 0xf0 },
	{ 0x3022, 0x01 },
	{ 0x3030, 0x10 },
	{ 0x3039, 0x32 },
	{ 0x303a, 0x00 },

	/* manual AEC/AGC */
	{ 0x3500, 0x00 },
	{ 0x3501, 0x2a },
	{ 0x3502, 0x90 },
	{ 0x3503, 0x08 },
	{ 0x3505, 0x8c },
	{ 0x3507, 0x03 },
	{ 0x3508, 0x00 },
	{ 0x3509, 0x10 },

	/* analog control */
	{ 0x3610, 0x80 },
	{ 0x3611, 0xa0 },
	{ 0x3620, 0x6e },
	{ 0x3632, 0x56 },
	{ 0x3633, 0x78 },
	{ 0x3662, 0x05 },
	{ 0x3666, 0x00 },
	{ 0x366f, 0x5a },
	{ 0x3680, 0x84 },

	/* sensor control */
	{ 0x3712, 0x80 },
	{ 0x372d, 0x22 },
	{ 0x3731, 0x80 },
	{ 0x3732, 0x30 },
	{ 0x3778, 0x00 },
	{ 0x377d, 0x22 },
	{ 0x3788, 0x02 },
	{ 0x3789, 0xa4 },
	{ 0x378a, 0x00 },
	{ 0x378b, 0x4a },
	{ 0x3799, 0x20 },

	/* timing control */
	{ 0x3800, 0x00 },
	{ 0x3801, 0x00 },
	{ 0x3802, 0x00 },
	{ 0x3803, 0x00 },
	{ 0x3804, 0x05 },
	{ 0x3805, 0x0f },
	{ 0x3806, 0x03 },
	{ 0x3807, 0x2f },
	{ 0x3808, 0x05 },
	{ 0x3809, 0x00 },
	{ 0x380a, 0x03 },
	{ 0x380b, 0x20 },
	{ 0x380c, 0x02 },
	{ 0x380d, 0xd8 },
	{ 0x380e, 0x07 },
	{ 0x380f, 0x1c },
	{ 0x3810, 0x00 },
	{ 0x3811, 0x08 },
	{ 0x3812, 0x00 },
	{ 0x3813, 0x08 },
	{ 0x3814, 0x11 },
	{ 0x3815, 0x11 },
	{ 0x3820, 0x00 },
	{ 0x3821, 0x00 },
	{ 0x382c, 0x05 },
	{ 0x382d, 0xb0 },
	{ 0x389d, 0x00 },
	{ 0x3881, 0x42 },
	{ 0x3882, 0x01 },
	{ 0x3883, 0x00 },
	{ 0x3885, 0x02 },
	{ 0x38a8, 0x02 },
	{ 0x38a9, 0x80 },
	{ 0x38b1, 0x00 },
	{ 0x38b3, 0x02 },
	{ 0x38c4, 0x00 },
	{ 0x38c5, 0xc0 },
	{ 0x38c6, 0x04 },
	{ 0x38c7, 0x80 },

	/* PWM and strobe control */
	{ 0x3920, 0xff },

	/* BLC control */
	{ 0x4003, 0x40 },
	{ 0x4008, 0x04 },
	{ 0x4009, 0x0b },
	{ 0x400c, 0x00 },
	{ 0x400d, 0x07 },
	{ 0x4010, 0x40 },
	{ 0x4043, 0x40 },

	/* format control */
	{ 0x4307, 0x30 },
	{ 0x4317, 0x00 },

	/* ???? */
	{ 0x4501, 0x00 },
	{ 0x4507, 0x00 },
	{ 0x4509, 0x00 },
	{ 0x450a, 0x08 },

	/* VFIFO control */
	{ 0x4601, 0x04 },

	/* DVP control */
	{ 0x470f, 0x00 },

	/* low power mode control */
	{ 0x4f07, 0x00 },

	/* MIPI top control */
	{ 0x4800, 0x00 }, /* bit 5: discontinuous clk */

	/* ISP top control */
	{ 0x5000, 0x9f },
	{ 0x5001, 0x00 },
	{ 0x5e00, 0x00 },

	/* ???? */
	{ 0x5d00, 0x07 },
	{ 0x5d01, 0x00 },

	/* low power mode control */
	{ 0x4f00, 0x04 },
	{ 0x4f10, 0x00 },
	{ 0x4f11, 0x98 },
	{ 0x4f12, 0x0f },
	{ 0x4f13, 0xc4 },
	{ OV9281_TABLE_END, 0x00 }
};

static const ov9281_reg ov9281_mode_1280x800_26MhzMCLK_fsync_slave[] = {
	{ 0x3826, 0x03 }, /* R reset value on fsin.  VTS - 4 */
	{ 0x3827, 0x8a },
	{ OV9281_TABLE_END, 0x00 }
};

static const ov9281_reg ov9281_mode_1280x720_26MhzMCLK[] = {
	{ 0x0302, 0x32 },
	{ 0x030d, 0x50 },
	{ 0x030e, 0x02 },
	{ 0x3001, 0x00 },
	{ 0x3004, 0x00 },
	{ 0x3005, 0x00 },
	{ 0x3006, 0x04 },
	{ 0x3011, 0x0a },
	{ 0x3013, 0x18 },
	{ 0x3022, 0x01 },
	{ 0x3030, 0x10 },
	{ 0x3039, 0x32 },
	{ 0x303a, 0x00 },
	{ 0x3500, 0x00 },
	{ 0x3501, 0x2a },
	{ 0x3502, 0x90 },
	{ 0x3503, 0x08 },
	{ 0x3505, 0x8c },
	{ 0x3507, 0x03 },
	{ 0x3508, 0x00 },
	{ 0x3509, 0x10 },
	{ 0x3610, 0x80 },
	{ 0x3611, 0xa0 },
	{ 0x3620, 0x6e },
	{ 0x3632, 0x56 },
	{ 0x3633, 0x78 },
	{ 0x3662, 0x05 },
	{ 0x3666, 0x00 },
	{ 0x366f, 0x5a },
	{ 0x3680, 0x84 },
	{ 0x3712, 0x80 },
	{ 0x372d, 0x22 },
	{ 0x3731, 0x80 },
	{ 0x3732, 0x30 },
	{ 0x3778, 0x00 },
	{ 0x377d, 0x22 },
	{ 0x3788, 0x02 },
	{ 0x3789, 0xa4 },
	{ 0x378a, 0x00 },
	{ 0x378b, 0x4a },
	{ 0x3799, 0x20 },
	{ 0x3800, 0x00 },
	{ 0x3801, 0x00 },
	{ 0x3802, 0x00 },
	{ 0x3803, 0x28 },
	{ 0x3804, 0x05 },
	{ 0x3805, 0x0f },
	{ 0x3806, 0x03 },
	{ 0x3807, 0x07 },
	{ 0x3808, 0x05 },
	{ 0x3809, 0x00 },
	{ 0x380a, 0x02 },
	{ 0x380b, 0xd0 },
	{ 0x380c, 0x02 },
	{ 0x380d, 0xd8 },
	{ 0x380e, 0x07 },
	{ 0x380f, 0x1c },
	{ 0x3810, 0x00 },
	{ 0x3811, 0x08 },
	{ 0x3812, 0x00 },
	{ 0x3813, 0x08 },
	{ 0x3814, 0x11 },
	{ 0x3815, 0x11 },
	{ 0x3820, 0x00 },
	{ 0x3821, 0x00 },
	{ 0x3881, 0x42 },
	{ 0x38a8, 0x02 },
	{ 0x38a9, 0x80 },
	{ 0x38b1, 0x00 },
	{ 0x38c4, 0x00 },
	{ 0x38c5, 0xc0 },
	{ 0x38c6, 0x04 },
	{ 0x38c7, 0x80 },
	{ 0x3920, 0xff },
	{ 0x4003, 0x40 },
	{ 0x4008, 0x04 },
	{ 0x4009, 0x0b },
	{ 0x400c, 0x00 },
	{ 0x400d, 0x07 },
	{ 0x4010, 0x40 },
	{ 0x4043, 0x40 },
	{ 0x4307, 0x30 },
	{ 0x4317, 0x00 },
	{ 0x4501, 0x00 },
	{ 0x4507, 0x00 },
	{ 0x4509, 0x00 },
	{ 0x450a, 0x08 },
	{ 0x4601, 0x04 },
	{ 0x470f, 0x00 },
	{ 0x4f07, 0x00 },
	{ 0x4800, 0x00 },
	{ 0x5000, 0x9f },
	{ 0x5001, 0x00 },
	{ 0x5e00, 0x00 },
	{ 0x5d00, 0x07 },
	{ 0x5d01, 0x00 },
	{ OV9281_TABLE_END, 0x00 }
};

static const ov9281_reg ov9281_mode_1280x720_26MhzMCLK_fsync_slave[] = {
	{ 0x3826, 0x03 }, /* R reset value on fsin.  VTS - 4 */
	{ 0x3827, 0x8a },
	{ OV9281_TABLE_END, 0x00 }
};

static const ov9281_reg ov9281_mode_640x400_26MhzMCLK[] = {
	{ 0x0302, 0x32 },
	{ 0x030d, 0x50 },
	{ 0x030e, 0x02 },
	{ 0x3001, 0x00 },
	{ 0x3004, 0x00 },
	{ 0x3005, 0x00 },
	{ 0x3006, 0x04 },
	{ 0x3011, 0x0a },
	{ 0x3013, 0x18 },
	{ 0x3022, 0x01 },
	{ 0x3030, 0x10 },
	{ 0x3039, 0x32 },
	{ 0x303a, 0x00 },
	{ 0x3500, 0x00 },
	{ 0x3501, 0x01 },
	{ 0x3502, 0xf4 },
	{ 0x3503, 0x08 },
	{ 0x3505, 0x8c },
	{ 0x3507, 0x03 },
	{ 0x3508, 0x00 },
	{ 0x3509, 0x10 },
	{ 0x3610, 0x80 },
	{ 0x3611, 0xa0 },
	{ 0x3620, 0x6e },
	{ 0x3632, 0x56 },
	{ 0x3633, 0x78 },
	{ 0x3662, 0x05 },
	{ 0x3666, 0x00 },
	{ 0x366f, 0x5a },
	{ 0x3680, 0x84 },
	{ 0x3712, 0x80 },
	{ 0x372d, 0x22 },
	{ 0x3731, 0x80 },
	{ 0x3732, 0x30 },
	{ 0x3778, 0x10 },
	{ 0x377d, 0x22 },
	{ 0x3788, 0x02 },
	{ 0x3789, 0xa4 },
	{ 0x378a, 0x00 },
	{ 0x378b, 0x4a },
	{ 0x3799, 0x20 },
	{ 0x3800, 0x00 },
	{ 0x3801, 0x00 },
	{ 0x3802, 0x00 },
	{ 0x3803, 0x00 },
	{ 0x3804, 0x05 },
	{ 0x3805, 0x0f },
	{ 0x3806, 0x03 },
	{ 0x3807, 0x2f },
	{ 0x3808, 0x02 },
	{ 0x3809, 0x80 },
	{ 0x380a, 0x01 },
	{ 0x380b, 0x90 },
	{ 0x380c, 0x02 },
	{ 0x380d, 0xd8 },
	{ 0x380e, 0x02 },
	{ 0x380f, 0x08 },
	{ 0x3810, 0x00 },
	{ 0x3811, 0x04 },
	{ 0x3812, 0x00 },
	{ 0x3813, 0x04 },
	{ 0x3814, 0x31 },
	{ 0x3815, 0x22 },
	{ 0x3820, 0x20 },
	{ 0x3821, 0x01 },
	{ 0x3881, 0x42 },
	{ 0x38a8, 0x02 },
	{ 0x38a9, 0x80 },
	{ 0x38b1, 0x00 },
	{ 0x38c4, 0x00 },
	{ 0x38c5, 0xc0 },
	{ 0x38c6, 0x04 },
	{ 0x38c7, 0x80 },
	{ 0x3920, 0xff },
	{ 0x4003, 0x40 },
	{ 0x4008, 0x02 },
	{ 0x4009, 0x05 },
	{ 0x400c, 0x00 },
	{ 0x400d, 0x03 },
	{ 0x4010, 0x40 },
	{ 0x4043, 0x40 },
	{ 0x4307, 0x30 },
	{ 0x4317, 0x00 },
	{ 0x4501, 0x00 },
	{ 0x4507, 0x03 },
	{ 0x4509, 0x80 },
	{ 0x450a, 0x08 },
	{ 0x4601, 0x04 },
	{ 0x470f, 0x00 },
	{ 0x4f07, 0x00 },
	{ 0x4800, 0x00 },
	{ 0x5000, 0x9f },
	{ 0x5001, 0x00 },
	{ 0x5e00, 0x00 },
	{ 0x5d00, 0x07 },
	{ 0x5d01, 0x00 },
	{ OV9281_TABLE_END, 0x00 }
};

static const ov9281_reg ov9281_mode_640x400_26MhzMCLK_fsync_slave[] = {
	{ 0x3826, 0x02 }, /* R reset value on fsin.  VTS - 4 */
	{ 0x3827, 0x04 },
	{ OV9281_TABLE_END, 0x00 }
};

static const ov9281_reg *ov9281_mode_table[] = {
	[OV9281_MODE_1280X800] = ov9281_mode_1280x800_26MhzMCLK,
	[OV9281_MODE_1280X720] = ov9281_mode_1280x720_26MhzMCLK,
	[OV9281_MODE_640X400] = ov9281_mode_640x400_26MhzMCLK,
	[OV9281_MODE_START_STREAM] = ov9281_start,
	[OV9281_MODE_STOP_STREAM] = ov9281_stop,
};

static const ov9281_reg *ov9281_fsync_slave_mode_table[] = {
	[OV9281_MODE_1280X800] = ov9281_mode_1280x800_26MhzMCLK_fsync_slave,
	[OV9281_MODE_1280X720] = ov9281_mode_1280x720_26MhzMCLK_fsync_slave,
	[OV9281_MODE_640X400] = ov9281_mode_640x400_26MhzMCLK_fsync_slave,
};

static const ov9281_reg *ov9281_fsync_table[] = {
	[OV9281_FSYNC_NONE] = NULL,
	[OV9281_FSYNC_MASTER] = ov9281_fsync_master,
	[OV9281_FSYNC_SLAVE] = ov9281_fsync_slave,
};

static const int ov9281_60fps[] = {
	60,
};

/*
 * ov9281_210fps[] — THÊM MỚI khi port sang tegracam v2.0 (không có trong bản
 * gốc NVIDIA 2016/R35.6.5). Sửa 1 BUG THẬT trong ov9281_frmfmt[] gốc: file
 * gốc gán chung ov9281_60fps cho cả 3 mode, kể cả mode 640x400 — trong khi
 * HTS/VTS thật của mode này (0x380c/d=728, 0x380e/f=520) cho pixel_clock=
 * 80MHz (đã tính và kiểm chứng ở Giai đoạn 2, xem block ghi chú cuối file)
 * ra fps thật ~211.3, không phải 60. Đây là lỗi có thật trong
 * ov9281_mode_tbls.h gốc của NVIDIA (2016) — phát hiện được khi đối chiếu
 * số liệu register thật với datasheet lúc port sang tegracam v2.0, không
 * phải lỗi phát sinh từ quá trình port. Dùng 210 (số nguyên, đúng theo
 * datasheet OV9281 công bố chính thức cho mode 640x400 4:1 sub-sampling)
 * thay vì làm tròn 211.3 lên 211, vì đây là con số OmniVision đã công bố
 * chính thức, đáng tin cậy hơn số tự làm tròn.
 */
static const int ov9281_210fps[] = {
	210,
};

static const struct camera_common_frmfmt ov9281_frmfmt[] = {
	{ { 1280, 800 }, ov9281_60fps, ARRAY_SIZE(ov9281_60fps), 0,
	  OV9281_MODE_1280X800 },
	{ { 1280, 720 }, ov9281_60fps, ARRAY_SIZE(ov9281_60fps), 0,
	  OV9281_MODE_1280X720 },
	/* SỬA so với bản gốc: ov9281_60fps -> ov9281_210fps (xem giải thích
	 * bug ở trên) */
	{ { 640, 400 }, ov9281_210fps, ARRAY_SIZE(ov9281_210fps), 0,
	  OV9281_MODE_640X400 },
};

/* =============================================================================
 * GHI CHÚ GIAI ĐOẠN 2 — pixel_clock/line_length cho .dtsi (Giai đoạn 3)
 * =============================================================================
 *
 * QUAN TRỌNG: struct `sensor_mode_properties` (pix_clk_hz, line_length,
 * active_w/h, framerate_factor, min/max_framerate...) KHÔNG được khai báo
 * bằng C trong file này (hay bất kỳ đâu trong nvidia-oot) — đã kiểm chứng
 * bằng cách đọc sensor_common.c (sensor_common_parse_num_modes() +
 * sensor_common_parse_image_props()): toàn bộ field này được framework đọc
 * TỪ DEVICE TREE lúc chạy (property của node mode0/mode1/mode2 trong
 * .dtsi), không có static initializer nào trong ov5693_mode_tbls.h/
 * nv_imx219.c/bất kỳ driver .c nào khác trong cây R39.2. Ví dụ thật đã đối
 * chiếu: tegra234-camera-rbpcv2-imx219.dtsi (IMX219 — sensor đang chạy ổn
 * trên board này) có node mode0/mode1 với đúng các property trên.
 *
 * → Số liệu dưới đây tính SẴN từ register table thật ở trên (HTS/VTS/PLL),
 * để dùng trực tiếp khi viết node mode0/mode1/mode2 trong .dtsi thật
 * (Giai đoạn 3) — không phải để paste vào file .h này.
 *
 * ---- Cách tính (không giải mã bit-field PLL 0x0302/0x030d/0x030e — không
 * có đủ bảng bit-field chính xác từ datasheet mục 2.8 để làm việc đó an
 * toàn) ----
 *
 *   pixel_clock (Hz) = HTS (pixel/dòng) × VTS (dòng/frame) × fps
 *   (đẳng thức thời gian sensor chuẩn — đúng công thức tegracam framework
 *   đã dùng, xem ov9281_set_frame_rate() đã port: frame_length =
 *   pixel_clock × framerate_factor / line_length / val)
 *
 *   HTS = thanh ghi 0x380c/0x380d, VTS = thanh ghi 0x380e/0x380f — đọc trực
 *   tiếp từ register table thật ở trên, cả 3 mode đều dùng chung PLL
 *   (0x0302=0x32, 0x030d=0x50, 0x030e=0x02) nên chung 1 pixel_clock.
 *
 *   Kiểm chứng: lấy pixel_clock = 80,000,000 Hz (SYS_CLK từ bảng mẫu
 *   datasheet mục 2.8.1 table 2-10, EXTCLK=24MHz — KHÔNG PHẢI 26MHz như tên
 *   hàm "ov9281_mode_1280x800_26MhzMCLK" gợi ý; đã xác nhận MCLK Jetson cấp
 *   thật = 24MHz, và tên hàm gốc là nhãn sai của NVIDIA, không phải giá trị
 *   PLL thật):
 *     fps = 80,000,000 / (728 × 1820) = 60.38 -> khớp ov9281_60fps[]={60}
 *     đã có sẵn trong file này -> xác nhận công thức + giá trị 80MHz đúng.
 *
 * ---- Số liệu từng mode (dùng cho .dtsi Giai đoạn 3) ----
 *
 *   Mode 0 — OV9281_MODE_1280X800 (ov9281_mode_1280x800_26MhzMCLK):
 *     active_w = 1280, active_h = 800
 *     line_length (HTS, 0x380c/d) = 728
 *     pix_clk_hz = 80000000
 *     fps thật ≈ 60.38 (khớp ov9281_60fps[]={60} gốc)
 *
 *   Mode 1 — OV9281_MODE_1280X720 (ov9281_mode_1280x720_26MhzMCLK):
 *     active_w = 1280, active_h = 720
 *     line_length (HTS) = 728, VTS = 1820 (giống hệt mode 0, chỉ khác cửa
 *     sổ crop 0x3800-0x3807, không khác timing tổng)
 *     pix_clk_hz = 80000000
 *     fps thật ≈ 60.38 (khớp ov9281_60fps[]={60} gốc)
 *
 *   Mode 2 — OV9281_MODE_640X400 (ov9281_mode_640x400_26MhzMCLK):
 *     active_w = 640, active_h = 400
 *     line_length (HTS) = 728, VTS = 520 (0x0208)
 *     pix_clk_hz = 80000000
 *     fps thật tính ra ≈ 211.3 (= 80000000/(728×520))
 *
 *     ⚠️ PHÁT HIỆN: ov9281_frmfmt[] ở trên gán ov9281_60fps[]={60} cho CẢ
 *     3 mode, kể cả mode này — SAI cho mode 640x400 (chỉ đúng cho mode 0/1).
 *     fps thật ~211.3 khớp rất sát 210fps datasheet công bố chính thức cho
 *     mode 640x400 (mục "Format/resolution hỗ trợ", 4:1 sub-sampling) — xác
 *     nhận đây là NVIDIA dùng lại nhầm/lười 1 mảng fps chung cho cả 3 mode
 *     trong bản gốc R35.6.5, không phải do PLL/HTS/VTS tính sai.
 *     QUYẾT ĐỊNH (đã xác nhận với người dùng): dùng max_framerate = 210fps
 *     (theo datasheet) cho property .dtsi mode2, KHÔNG dùng 60fps gốc.
 *     ✅ ĐÃ SỬA: thêm mảng ov9281_210fps[]={210} riêng, gán cho mode
 *     640x400 trong ov9281_frmfmt[] ở trên (thay vì dùng chung
 *     ov9281_60fps như bản gốc) — ENUM_FRAMEINTERVALS của V4L2 giờ báo
 *     đúng. Đây là 1 bug có thật trong ov9281_mode_tbls.h gốc của NVIDIA
 *     (2016, R35.6.5) — phát hiện được khi đối chiếu số liệu register thật
 *     với datasheet lúc port sang tegracam v2.0 (không phải lỗi phát sinh
 *     từ quá trình port), đáng ghi lại khi viết README dự án.
 *
 *   mclk_multiplier (tham khảo, không bắt buộc) = pix_clk_hz / mclk_hz
 *     = 80000000 / 24000000 = 3.333...
 * ============================================================================= */

#endif  /* __OV9281_I2C_TABLES__ */
