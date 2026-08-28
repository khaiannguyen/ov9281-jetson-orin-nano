/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2016-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
/*
 * Đích cuối cùng: nvidia-oot/include/trace/events/ov9281.h
 * (include trong nv_ov9281.c là <trace/events/ov9281.h>, cùng với
 * #define CREATE_TRACE_POINTS ngay phía trên)
 *
 * Tối thiểu/rỗng theo đúng khuôn <trace/events/ov5693.h>, nhưng KHÔNG khai
 * TRACE_EVENT nào — đã grep xác nhận: ngay cả nv_ov5693.c thật (khuôn mẫu,
 * R39.2) include header tương ứng + CREATE_TRACE_POINTS nhưng KHÔNG hề gọi
 * trace_ov5693_s_stream() ở bất kỳ đâu trong file. Vậy tracepoint đó vốn đã
 * không được dùng ngay cả ở bản gốc OV5693 chạy thật — không có lý do thêm
 * 1 TRACE_EVENT tương tự (trace_ov9281_s_stream) cho OV9281 nếu không có nơi
 * nào gọi nó. Giữ khung tracepoint hợp lệ (để #include không lỗi và
 * CREATE_TRACE_POINTS không rỗng-vô-nghĩa), thêm TRACE_EVENT thật sau nếu
 * sau này cần tracing chi tiết (ví dụ debug race điều kiện 2 camera chạy
 * đồng thời).
 */

#include <nvidia/conftest.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ov9281

#if !defined(_TRACE_OV9281_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_OV9281_H

#include <linux/tracepoint.h>

#endif

/* This part must be outside protection */
#include <trace/define_trace.h>
