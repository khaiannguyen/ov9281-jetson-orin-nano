/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Đích cuối cùng: nvidia-oot/include/media/ov9281.h
 * (include trong nv_ov9281.c là <media/ov9281.h>)
 */

#ifndef __OV9281_H__
#define __OV9281_H__

/*
 * Cố ý để TRỐNG.
 *
 * Đã đối chiếu <media/ov5693.h> (khuôn mẫu) và audit toàn bộ
 * nv_ov9281_ported_partial.c: không có struct/define nào thực sự cần lấy từ
 * header này. Mọi register address (OV9281_SC_*, OV9281_TIMING_FORMAT*,
 * OV9281_GROUP_HOLD_*...), kích thước OTP/fuse ID
 * (OV9281_OTP_BUFFER_SIZE, OV9281_FUSE_ID_OTP_BUFFER_SIZE...), địa chỉ I2C
 * mặc định (OV9281_DEFAULT_I2C_ADDRESS_*), và struct ov9281 đều đã khai báo
 * trực tiếp trong nv_ov9281.c hoặc kế thừa từ ov9281_mode_tbls.h (R35.6.5).
 *
 * Bản gốc R35.6.5 (nv_ov9281.c) vốn KHÔNG hề include <media/ov9281.h> —
 * header này chỉ tồn tại vì include đó đã được thêm vào bản port ở phiên
 * trước, theo khuôn OV5693, trước khi đối chiếu kỹ xem có thật sự cần không.
 *
 * Cố tình KHÔNG copy các struct riêng của OV5693 không liên quan tới OV9281
 * (ov5693_eeprom_data, ov5693_power_rail, ov5693_regulators, OV5693_EEPROM_*,
 * OV5693_OTP_* — OV9281 không có EEPROM, và power_rail/regulators đã dùng
 * thẳng camera_common_power_rail/camera_common_regulators chuẩn của
 * tegracam v2.0, không cần struct riêng như OV5693 làm).
 *
 * Nếu về sau phát hiện thật sự cần khai báo gì dùng chung ở đây (ví dụ nếu
 * có thêm 1 file .c khác cũng cần struct ov9281), thêm vào lúc đó — không
 * thêm trước khi có nhu cầu thật.
 */

#endif /* __OV9281_H__ */
