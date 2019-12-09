// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip ISP1 Driver - V4l capture device
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>

#include "common.h"

/*
 * NOTE:
 * 1. There are two capture video devices in rkisp1, selfpath and mainpath
 * 2. Two capture device have separated memory-interface/crop/scale units.
 * 3. Besides describing stream hardware, this file also contain entries
 *    for pipeline operations.
 * 4. The register read/write operations in this file are put into regs.c.
 */

/*
 * differences between selfpath and mainpath
 * available mp sink input: isp
 * available sp sink input : isp, dma(TODO)
 * available mp sink pad fmts: yuv422, raw
 * available sp sink pad fmts: yuv422, yuv420......
 * available mp source fmts: yuv, raw, jpeg(TODO)
 * available sp source fmts: yuv, rgb
 */

#define RKISP1_MIN_BUFFERS_NEEDED 3
//#define RKISP1_CIF_ISP_REQ_BUFS_MAX 8

#define RKISP1_RSZ_MP_OUT_MAX_WIDTH		4416
#define RKISP1_RSZ_MP_OUT_MAX_HEIGHT		3312
#define RKISP1_RSZ_SP_OUT_MAX_WIDTH		1920
#define RKISP1_RSZ_SP_OUT_MAX_HEIGHT		1920
#define RKISP1_RSZ_OUT_MIN_WIDTH		32
#define RKISP1_RSZ_OUT_MIN_HEIGHT		16

#define RKISP1_IN_MIN_WIDTH		32
#define RKISP1_IN_MIN_HEIGHT		32

#define RKISP1_MBUS_FMT_HDIV 2
#define RKISP1_MBUS_FMT_VDIV 1

/* Considering self path bus format MEDIA_BUS_FMT_YUYV8_2X8 */
#define RKISP1_SP_IN_FMT RKISP1_MI_CTRL_SP_INPUT_YUV422

/**
 * enum rkisp1_shadow_regs_when - define when to update shadow registers
 *
 * @RKISP1_SHADOW_REGS_SYNC	update shadow registers immediatly
 * @RKISP1_SHADOW_REGS_ASYNC	update shadow registers syncronized with frames
 *
 * Struct which matches the MEDIA_BUS_FMT_* codes with the corresponding
 * V4L2_PIX_FMT_* fourcc pixelformat and its bytes per pixel (bpp)
 */
enum rkisp1_shadow_regs_when {
	RKISP1_SHADOW_REGS_SYNC,
	RKISP1_SHADOW_REGS_ASYNC,
};

/*
 * @fourcc: pixel format
 * @mbus_code: pixel format over bus
 * @fmt_type: helper filed for pixel format
 * @bayer_pat: bayer patten type
 * @uv_swap: if cb cr swaped, for yuv
 * @write_format: defines how YCbCr self picture data is written to memory
 * @input_format: defines sp input format
 * @output_format: defines sp output format
 */
struct rkisp1_stream_fmt {
	u32 fourcc;
	u32 mbus_code;
	u8 fmt_type;
	u8 uv_swap;
	u32 write_format;
	u32 output_format;
};

/* Different config between selfpath and mainpath */
struct rkisp1_stream_cfg {
	const struct rkisp1_stream_fmt *fmts;
	int fmt_size;
	/* constrains */
	const int max_rsz_width;
	const int max_rsz_height;
	const int min_rsz_width;
	const int min_rsz_height;
	/* registers */
	struct {
		u32 ctrl;
		u32 ctrl_shd;
		u32 scale_hy;
		u32 scale_hcr;
		u32 scale_hcb;
		u32 scale_vy;
		u32 scale_vc;
		u32 scale_lut;
		u32 scale_lut_addr;
		u32 scale_hy_shd;
		u32 scale_hcr_shd;
		u32 scale_hcb_shd;
		u32 scale_vy_shd;
		u32 scale_vc_shd;
		u32 phase_hy;
		u32 phase_hc;
		u32 phase_vy;
		u32 phase_vc;
		u32 phase_hy_shd;
		u32 phase_hc_shd;
		u32 phase_vy_shd;
		u32 phase_vc_shd;
	} rsz;
	struct {
		u32 ctrl;
		u32 yuvmode_mask;
		u32 rawmode_mask;
		u32 h_offset;
		u32 v_offset;
		u32 h_size;
		u32 v_size;
	} dual_crop;
	struct {
		u32 y_size_init;
		u32 cb_size_init;
		u32 cr_size_init;
		u32 y_base_ad_init;
		u32 cb_base_ad_init;
		u32 cr_base_ad_init;
		u32 y_offs_cnt_init;
		u32 cb_offs_cnt_init;
		u32 cr_offs_cnt_init;
	} mi;
};

/* Different reg ops between selfpath and mainpath */
struct rkisp1_streams_ops {
	int (*config)(struct rkisp1_stream *stream);
	void (*stop)(struct rkisp1_stream *stream);
	void (*enable)(struct rkisp1_stream *stream);
	void (*disable)(struct rkisp1_stream *stream);
	void (*set_data_path)(struct rkisp1_stream *stream);
	bool (*is_stopped)(struct rkisp1_stream *stream);
};

static const struct rkisp1_stream_fmt rkisp1_mp_fmts[] = {
	/* yuv422 */
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUVINT,
	}, {
		.fourcc = V4L2_PIX_FMT_YVYU,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUVINT,
	}, {
		.fourcc = V4L2_PIX_FMT_VYUY,
		.fmt_type = RKISP1_FMT_YUV,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUVINT,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_NV16,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_NV61,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_YVU422M,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	},
	/* yuv420 */
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_NV21M,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12M,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV420,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_YVU420,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	},
	/* yuv444 */
	{
		.fourcc = V4L2_PIX_FMT_YUV444M,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	},
	/* yuv400 */
	{
		.fourcc = V4L2_PIX_FMT_GREY,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUVINT,
	},
	/* raw */
	{
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB10,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG10,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG10,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR10,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB12,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG12,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG12,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR12,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
	},
};

static const struct rkisp1_stream_fmt rkisp1_sp_fmts[] = {
	/* yuv422 */
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_INT,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_YVYU,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_INT,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_VYUY,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_INT,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_NV16,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_SPLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_NV61,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_SPLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_YVU422M,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
	},
	/* yuv420 */
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_SPLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_SPLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_NV21M,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_SPLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12M,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_SPLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV420,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_YVU420,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV420,
	},
	/* yuv444 */
	{
		.fourcc = V4L2_PIX_FMT_YUV444M,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV444,
	},
	/* yuv400 */
	{
		.fourcc = V4L2_PIX_FMT_GREY,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_INT,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV400,
	},
	/* rgb */
	{
		.fourcc = V4L2_PIX_FMT_RGB24,
		.fmt_type = RKISP1_FMT_RGB,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_RGB888,
	}, {
		.fourcc = V4L2_PIX_FMT_RGB565,
		.fmt_type = RKISP1_FMT_RGB,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_RGB565,
	}, {
		.fourcc = V4L2_PIX_FMT_BGR666,
		.fmt_type = RKISP1_FMT_RGB,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_RGB666,
	},
};

static const struct rkisp1_stream_cfg rkisp1_mp_stream_config = {
	.fmts = rkisp1_mp_fmts,
	.fmt_size = ARRAY_SIZE(rkisp1_mp_fmts),
	/* constraints */
	.max_rsz_width = RKISP1_RSZ_MP_OUT_MAX_WIDTH,
	.max_rsz_height = RKISP1_RSZ_MP_OUT_MAX_HEIGHT,
	.min_rsz_width = RKISP1_RSZ_OUT_MIN_WIDTH,
	.min_rsz_height = RKISP1_RSZ_OUT_MIN_HEIGHT,
	/* registers */
	.rsz = {
		.ctrl =			RKISP1_CIF_MRSZ_CTRL,
		.scale_hy =		RKISP1_CIF_MRSZ_SCALE_HY,
		.scale_hcr =		RKISP1_CIF_MRSZ_SCALE_HCR,
		.scale_hcb =		RKISP1_CIF_MRSZ_SCALE_HCB,
		.scale_vy =		RKISP1_CIF_MRSZ_SCALE_VY,
		.scale_vc =		RKISP1_CIF_MRSZ_SCALE_VC,
		.scale_lut =		RKISP1_CIF_MRSZ_SCALE_LUT,
		.scale_lut_addr =	RKISP1_CIF_MRSZ_SCALE_LUT_ADDR,
		.scale_hy_shd =		RKISP1_CIF_MRSZ_SCALE_HY_SHD,
		.scale_hcr_shd =	RKISP1_CIF_MRSZ_SCALE_HCR_SHD,
		.scale_hcb_shd =	RKISP1_CIF_MRSZ_SCALE_HCB_SHD,
		.scale_vy_shd =		RKISP1_CIF_MRSZ_SCALE_VY_SHD,
		.scale_vc_shd =		RKISP1_CIF_MRSZ_SCALE_VC_SHD,
		.phase_hy =		RKISP1_CIF_MRSZ_PHASE_HY,
		.phase_hc =		RKISP1_CIF_MRSZ_PHASE_HC,
		.phase_vy =		RKISP1_CIF_MRSZ_PHASE_VY,
		.phase_vc =		RKISP1_CIF_MRSZ_PHASE_VC,
		.ctrl_shd =		RKISP1_CIF_MRSZ_CTRL_SHD,
		.phase_hy_shd =		RKISP1_CIF_MRSZ_PHASE_HY_SHD,
		.phase_hc_shd =		RKISP1_CIF_MRSZ_PHASE_HC_SHD,
		.phase_vy_shd =		RKISP1_CIF_MRSZ_PHASE_VY_SHD,
		.phase_vc_shd =		RKISP1_CIF_MRSZ_PHASE_VC_SHD,
	},
	.dual_crop = {
		.ctrl =			RKISP1_CIF_DUAL_CROP_CTRL,
		.yuvmode_mask =		RKISP1_CIF_DUAL_CROP_MP_MODE_YUV,
		.rawmode_mask =		RKISP1_CIF_DUAL_CROP_MP_MODE_RAW,
		.h_offset =		RKISP1_CIF_DUAL_CROP_M_H_OFFS,
		.v_offset =		RKISP1_CIF_DUAL_CROP_M_V_OFFS,
		.h_size =		RKISP1_CIF_DUAL_CROP_M_H_SIZE,
		.v_size =		RKISP1_CIF_DUAL_CROP_M_V_SIZE,
	},
	.mi = {
		.y_size_init =		RKISP1_CIF_MI_MP_Y_SIZE_INIT,
		.cb_size_init =		RKISP1_CIF_MI_MP_CB_SIZE_INIT,
		.cr_size_init =		RKISP1_CIF_MI_MP_CR_SIZE_INIT,
		.y_base_ad_init =	RKISP1_CIF_MI_MP_Y_BASE_AD_INIT,
		.cb_base_ad_init =	RKISP1_CIF_MI_MP_CB_BASE_AD_INIT,
		.cr_base_ad_init =	RKISP1_CIF_MI_MP_CR_BASE_AD_INIT,
		.y_offs_cnt_init =	RKISP1_CIF_MI_MP_Y_OFFS_CNT_INIT,
		.cb_offs_cnt_init =	RKISP1_CIF_MI_MP_CB_OFFS_CNT_INIT,
		.cr_offs_cnt_init =	RKISP1_CIF_MI_MP_CR_OFFS_CNT_INIT,
	},
};

static const struct rkisp1_stream_cfg rkisp1_sp_stream_config = {
	.fmts = rkisp1_sp_fmts,
	.fmt_size = ARRAY_SIZE(rkisp1_sp_fmts),
	/* constraints */
	.max_rsz_width = RKISP1_RSZ_SP_OUT_MAX_WIDTH,
	.max_rsz_height = RKISP1_RSZ_SP_OUT_MAX_HEIGHT,
	.min_rsz_width = RKISP1_RSZ_OUT_MIN_WIDTH,
	.min_rsz_height = RKISP1_RSZ_OUT_MIN_HEIGHT,
	/* registers */
	.rsz = {
		.ctrl =			RKISP1_CIF_SRSZ_CTRL,
		.scale_hy =		RKISP1_CIF_SRSZ_SCALE_HY,
		.scale_hcr =		RKISP1_CIF_SRSZ_SCALE_HCR,
		.scale_hcb =		RKISP1_CIF_SRSZ_SCALE_HCB,
		.scale_vy =		RKISP1_CIF_SRSZ_SCALE_VY,
		.scale_vc =		RKISP1_CIF_SRSZ_SCALE_VC,
		.scale_lut =		RKISP1_CIF_SRSZ_SCALE_LUT,
		.scale_lut_addr =	RKISP1_CIF_SRSZ_SCALE_LUT_ADDR,
		.scale_hy_shd =		RKISP1_CIF_SRSZ_SCALE_HY_SHD,
		.scale_hcr_shd =	RKISP1_CIF_SRSZ_SCALE_HCR_SHD,
		.scale_hcb_shd =	RKISP1_CIF_SRSZ_SCALE_HCB_SHD,
		.scale_vy_shd =		RKISP1_CIF_SRSZ_SCALE_VY_SHD,
		.scale_vc_shd =		RKISP1_CIF_SRSZ_SCALE_VC_SHD,
		.phase_hy =		RKISP1_CIF_SRSZ_PHASE_HY,
		.phase_hc =		RKISP1_CIF_SRSZ_PHASE_HC,
		.phase_vy =		RKISP1_CIF_SRSZ_PHASE_VY,
		.phase_vc =		RKISP1_CIF_SRSZ_PHASE_VC,
		.ctrl_shd =		RKISP1_CIF_SRSZ_CTRL_SHD,
		.phase_hy_shd =		RKISP1_CIF_SRSZ_PHASE_HY_SHD,
		.phase_hc_shd =		RKISP1_CIF_SRSZ_PHASE_HC_SHD,
		.phase_vy_shd =		RKISP1_CIF_SRSZ_PHASE_VY_SHD,
		.phase_vc_shd =		RKISP1_CIF_SRSZ_PHASE_VC_SHD,
	},
	.dual_crop = {
		.ctrl =			RKISP1_CIF_DUAL_CROP_CTRL,
		.yuvmode_mask =		RKISP1_CIF_DUAL_CROP_SP_MODE_YUV,
		.rawmode_mask =		RKISP1_CIF_DUAL_CROP_SP_MODE_RAW,
		.h_offset =		RKISP1_CIF_DUAL_CROP_S_H_OFFS,
		.v_offset =		RKISP1_CIF_DUAL_CROP_S_V_OFFS,
		.h_size =		RKISP1_CIF_DUAL_CROP_S_H_SIZE,
		.v_size =		RKISP1_CIF_DUAL_CROP_S_V_SIZE,
	},
	.mi = {
		.y_size_init =		RKISP1_CIF_MI_SP_Y_SIZE_INIT,
		.cb_size_init =		RKISP1_CIF_MI_SP_CB_SIZE_INIT,
		.cr_size_init =		RKISP1_CIF_MI_SP_CR_SIZE_INIT,
		.y_base_ad_init =	RKISP1_CIF_MI_SP_Y_BASE_AD_INIT,
		.cb_base_ad_init =	RKISP1_CIF_MI_SP_CB_BASE_AD_INIT,
		.cr_base_ad_init =	RKISP1_CIF_MI_SP_CR_BASE_AD_INIT,
		.y_offs_cnt_init =	RKISP1_CIF_MI_SP_Y_OFFS_CNT_INIT,
		.cb_offs_cnt_init =	RKISP1_CIF_MI_SP_CB_OFFS_CNT_INIT,
		.cr_offs_cnt_init =	RKISP1_CIF_MI_SP_CR_OFFS_CNT_INIT,
	},
};

/* ----------------------------------------------------------------------------
 * Dual crop
 */

static void rkisp1_dcrop_disable(struct rkisp1_stream *stream,
				 enum rkisp1_shadow_regs_when when)
{
	u32 dc_ctrl = rkisp1_read(stream->rkisp1, stream->config->dual_crop.ctrl);
	u32 mask = ~(stream->config->dual_crop.yuvmode_mask |
			stream->config->dual_crop.rawmode_mask);

	dc_ctrl &= mask;
	if (when == RKISP1_SHADOW_REGS_ASYNC)
		dc_ctrl |= RKISP1_CIF_DUAL_CROP_GEN_CFG_UPD;
	else
		dc_ctrl |= RKISP1_CIF_DUAL_CROP_CFG_UPD;
	rkisp1_write(stream->rkisp1, dc_ctrl, stream->config->dual_crop.ctrl);
}

/* configure dual-crop unit */
static int rkisp1_dcrop_config(struct rkisp1_stream *stream)
{
	struct rkisp1_device *rkisp1 = stream->rkisp1;
	struct v4l2_rect *dcrop = &stream->dcrop;
	const struct v4l2_rect *input_win;
	u32 dc_ctrl;

	/* dual-crop unit get data from ISP */
	input_win = rkisp1_isp_sd_get_pad_crop(&rkisp1->isp_sdev, NULL,
					       RKISP1_ISP_PAD_SINK_VIDEO,
					       V4L2_SUBDEV_FORMAT_ACTIVE);

	if (dcrop->width == input_win->width &&
	    dcrop->height == input_win->height &&
	    dcrop->left == 0 && dcrop->top == 0) {
		rkisp1_dcrop_disable(stream, RKISP1_SHADOW_REGS_SYNC);
		dev_dbg(rkisp1->dev, "stream %d crop disabled\n", stream->id);
		return 0;
	}

	dc_ctrl = rkisp1_read(rkisp1, stream->config->dual_crop.ctrl);
	rkisp1_write(rkisp1, dcrop->left, stream->config->dual_crop.h_offset);
	rkisp1_write(rkisp1, dcrop->top, stream->config->dual_crop.v_offset);
	rkisp1_write(rkisp1, dcrop->width, stream->config->dual_crop.h_size);
	rkisp1_write(rkisp1, dcrop->height, stream->config->dual_crop.v_size);
	/* TODO: this is a mask, shouldn't it be dc_ctrl & ~mask ?*/
	dc_ctrl |= stream->config->dual_crop.yuvmode_mask;
	dc_ctrl |= RKISP1_CIF_DUAL_CROP_CFG_UPD;
	rkisp1_write(rkisp1, dc_ctrl, stream->config->dual_crop.ctrl);

	dev_dbg(rkisp1->dev, "stream %d crop: %dx%d -> %dx%d\n", stream->id,
		input_win->width, input_win->height,
		dcrop->width, dcrop->height);

	return 0;
}

/* ----------------------------------------------------------------------------
 * Resizer
 */

static void rkisp1_rsz_dump_regs(struct rkisp1_stream *stream)
{
	dev_dbg(stream->rkisp1->dev,
		"RSZ_CTRL 0x%08x/0x%08x\n"
		"RSZ_SCALE_HY %d/%d\n"
		"RSZ_SCALE_HCB %d/%d\n"
		"RSZ_SCALE_HCR %d/%d\n"
		"RSZ_SCALE_VY %d/%d\n"
		"RSZ_SCALE_VC %d/%d\n"
		"RSZ_PHASE_HY %d/%d\n"
		"RSZ_PHASE_HC %d/%d\n"
		"RSZ_PHASE_VY %d/%d\n"
		"RSZ_PHASE_VC %d/%d\n",
		rkisp1_read(stream->rkisp1, stream->config->rsz.ctrl),
		rkisp1_read(stream->rkisp1, stream->config->rsz.ctrl_shd),
		rkisp1_read(stream->rkisp1, stream->config->rsz.scale_hy),
		rkisp1_read(stream->rkisp1, stream->config->rsz.scale_hy_shd),
		rkisp1_read(stream->rkisp1, stream->config->rsz.scale_hcb),
		rkisp1_read(stream->rkisp1, stream->config->rsz.scale_hcb_shd),
		rkisp1_read(stream->rkisp1, stream->config->rsz.scale_hcr),
		rkisp1_read(stream->rkisp1, stream->config->rsz.scale_hcr_shd),
		rkisp1_read(stream->rkisp1, stream->config->rsz.scale_vy),
		rkisp1_read(stream->rkisp1, stream->config->rsz.scale_vy_shd),
		rkisp1_read(stream->rkisp1, stream->config->rsz.scale_vc),
		rkisp1_read(stream->rkisp1, stream->config->rsz.scale_vc_shd),
		rkisp1_read(stream->rkisp1, stream->config->rsz.phase_hy),
		rkisp1_read(stream->rkisp1, stream->config->rsz.phase_hy_shd),
		rkisp1_read(stream->rkisp1, stream->config->rsz.phase_hc),
		rkisp1_read(stream->rkisp1, stream->config->rsz.phase_hc_shd),
		rkisp1_read(stream->rkisp1, stream->config->rsz.phase_vy),
		rkisp1_read(stream->rkisp1, stream->config->rsz.phase_vy_shd),
		rkisp1_read(stream->rkisp1, stream->config->rsz.phase_vc),
		rkisp1_read(stream->rkisp1, stream->config->rsz.phase_vc_shd));
}

static void rkisp1_rsz_update_shadow(struct rkisp1_stream *stream,
				     enum rkisp1_shadow_regs_when when)
{
	u32 ctrl_cfg = rkisp1_read(stream->rkisp1, stream->config->rsz.ctrl);

	if (when == RKISP1_SHADOW_REGS_ASYNC)
		ctrl_cfg |= RKISP1_CIF_RSZ_CTRL_CFG_UPD_AUTO;
	else
		ctrl_cfg |= RKISP1_CIF_RSZ_CTRL_CFG_UPD;

	rkisp1_write(stream->rkisp1, ctrl_cfg, stream->config->rsz.ctrl);
}

static u32 rkisp1_rsz_calc_ratio(u32 len_in, u32 len_out)
{
	if (len_in < len_out)
		return ((len_in - 1) * RKISP1_CIF_RSZ_SCALER_FACTOR) /
		       (len_out - 1);

	return ((len_out - 1) * RKISP1_CIF_RSZ_SCALER_FACTOR) /
	       (len_in - 1) + 1;
}

static void rkisp1_rsz_disable(struct rkisp1_stream *stream,
			       enum rkisp1_shadow_regs_when when)
{
	rkisp1_write(stream->rkisp1, 0, stream->config->rsz.ctrl);

	if (when == RKISP1_SHADOW_REGS_SYNC)
		rkisp1_rsz_update_shadow(stream, when);
}

static void rkisp1_rsz_config_regs(struct rkisp1_stream *stream,
				   struct v4l2_rect *in_y,
				   struct v4l2_rect *in_c,
				   struct v4l2_rect *out_y,
				   struct v4l2_rect *out_c,
				   enum rkisp1_shadow_regs_when when)
{
	struct rkisp1_device *rkisp1 = stream->rkisp1;
	u32 ratio, rsz_ctrl = 0;
	unsigned int i;

	/* No phase offset */
	rkisp1_write(rkisp1, 0, stream->config->rsz.phase_hy);
	rkisp1_write(rkisp1, 0, stream->config->rsz.phase_hc);
	rkisp1_write(rkisp1, 0, stream->config->rsz.phase_vy);
	rkisp1_write(rkisp1, 0, stream->config->rsz.phase_vc);

	/* Linear interpolation */
	for (i = 0; i < 64; i++) {
		rkisp1_write(rkisp1, i, stream->config->rsz.scale_lut_addr);
		rkisp1_write(rkisp1, i, stream->config->rsz.scale_lut);
	}

	if (in_y->width != out_y->width) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_HY_ENABLE;
		if (in_y->width < out_y->width)
			rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_HY_UP;
		ratio = rkisp1_rsz_calc_ratio(in_y->width, out_y->width);
		rkisp1_write(rkisp1, ratio, stream->config->rsz.scale_hy);
	}

	if (in_c->width != out_c->width) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_HC_ENABLE;
		if (in_c->width < out_c->width)
			rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_HC_UP;
		ratio = rkisp1_rsz_calc_ratio(in_c->width, out_c->width);
		rkisp1_write(rkisp1, ratio, stream->config->rsz.scale_hcb);
		rkisp1_write(rkisp1, ratio, stream->config->rsz.scale_hcr);
	}

	if (in_y->height != out_y->height) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_VY_ENABLE;
		if (in_y->height < out_y->height)
			rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_VY_UP;
		ratio = rkisp1_rsz_calc_ratio(in_y->height, out_y->height);
		rkisp1_write(rkisp1, ratio, stream->config->rsz.scale_vy);
	}

	if (in_c->height != out_c->height) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_VC_ENABLE;
		if (in_c->height < out_c->height)
			rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_VC_UP;
		ratio = rkisp1_rsz_calc_ratio(in_c->height, out_c->height);
		rkisp1_write(rkisp1, ratio, stream->config->rsz.scale_vc);
	}

	rkisp1_write(rkisp1, rsz_ctrl, stream->config->rsz.ctrl);

	rkisp1_rsz_update_shadow(stream, when);
}

static int rkisp1_rsz_config(struct rkisp1_stream *stream,
			     enum rkisp1_shadow_regs_when when)
{
	const struct rkisp1_stream_fmt *output_isp_fmt = stream->out_isp_fmt;
	const struct rkisp1_fmt *input_isp_fmt = stream->rkisp1->isp_sdev.out_fmt;
	u8 hdiv = RKISP1_MBUS_FMT_HDIV, vdiv = RKISP1_MBUS_FMT_VDIV;
	struct v4l2_pix_format_mplane output_fmt = stream->out_fmt;
	struct v4l2_rect in_y, in_c, out_y, out_c;

	if (input_isp_fmt->fmt_type == RKISP1_FMT_BAYER)
		goto disable;

	in_y.width = stream->dcrop.width;
	in_y.height = stream->dcrop.height;
	out_y.width = output_fmt.width;
	out_y.height = output_fmt.height;

	in_c.width = in_y.width / RKISP1_MBUS_FMT_HDIV;
	in_c.height = in_y.height / RKISP1_MBUS_FMT_VDIV;

	if (output_isp_fmt->fmt_type == RKISP1_FMT_YUV) {
		const struct v4l2_format_info *pixfmt_info =
				v4l2_format_info(output_isp_fmt->fourcc);

		hdiv = pixfmt_info->hdiv;
		vdiv = pixfmt_info->vdiv;
	}
	out_c.width = out_y.width / hdiv;
	out_c.height = out_y.height / vdiv;

	/* TODO: why this doesn't check in_y out_y ? */
	if (in_c.width == out_c.width && in_c.height == out_c.height)
		goto disable;

	dev_dbg(stream->rkisp1->dev, "stream %d rsz/scale: %dx%d -> %dx%d\n",
		stream->id, stream->dcrop.width, stream->dcrop.height,
		output_fmt.width, output_fmt.height);
	dev_dbg(stream->rkisp1->dev, "chroma scaling %dx%d -> %dx%d\n",
		in_c.width, in_c.height, out_c.width, out_c.height);

	/* set values in the hw */
	rkisp1_rsz_config_regs(stream, &in_y, &in_c, &out_y, &out_c, when);

	rkisp1_rsz_dump_regs(stream);

	return 0;

disable:
	rkisp1_rsz_disable(stream, when);

	return 0;
}

/* ----------------------------------------------------------------------------
 * Stream operations for self-picture path (sp) and main-picture path (mp)
 */

static void rkisp1_mi_config_ctrl(struct rkisp1_stream *stream)
{
	u32 mi_ctrl = rkisp1_read(stream->rkisp1, RKISP1_CIF_MI_CTRL);

	mi_ctrl &= ~GENMASK(17, 16);
	mi_ctrl |= RKISP1_CIF_MI_CTRL_BURST_LEN_LUM_64;

	mi_ctrl &= ~GENMASK(19, 18);
	mi_ctrl |= RKISP1_CIF_MI_CTRL_BURST_LEN_CHROM_64;

	mi_ctrl |= RKISP1_CIF_MI_CTRL_INIT_BASE_EN |
		   RKISP1_CIF_MI_CTRL_INIT_OFFSET_EN;

	rkisp1_write(stream->rkisp1, mi_ctrl, RKISP1_CIF_MI_CTRL);
}

static u32 rkisp1_pixfmt_comp_size(const struct v4l2_pix_format_mplane *pixm,
				   unsigned int component)
{
	/*
	 * If packed format, then plane_fmt[0].sizeimage is the sum of all
	 * components, so we need to calculate just the size of Y component.
	 * See rkisp1_fill_pixfmt().
	 */
	if (!component && pixm->num_planes == 1)
		return pixm->plane_fmt[0].bytesperline * pixm->height;
	return pixm->plane_fmt[component].sizeimage;
}

static void rkisp1_irq_frame_end_enable(struct rkisp1_stream *stream)
{
	u32 mi_imsc = rkisp1_read(stream->rkisp1, RKISP1_CIF_MI_IMSC);

	mi_imsc |= RKISP1_CIF_MI_FRAME(stream);
	rkisp1_write(stream->rkisp1, mi_imsc, RKISP1_CIF_MI_IMSC);
}

/*
 * configure memory interface for mainpath
 * This should only be called when stream-on
 */
static int rkisp1_stream_mp_config(struct rkisp1_stream *stream)
{
	const struct v4l2_pix_format_mplane *pixm = &stream->out_fmt;
	struct rkisp1_device *rkisp1 = stream->rkisp1;
	u32 reg;

	rkisp1_write(rkisp1, rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_Y),
		     stream->config->mi.y_size_init);
	rkisp1_write(rkisp1, rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_CB),
		     stream->config->mi.cb_size_init);
	rkisp1_write(rkisp1, rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_CR),
		     stream->config->mi.cr_size_init);

	rkisp1_irq_frame_end_enable(stream);
	if (stream->out_isp_fmt->uv_swap) {
		reg = rkisp1_read(rkisp1, RKISP1_CIF_MI_XTD_FORMAT_CTRL);

		reg = (reg & ~BIT(0)) |
		      RKISP1_CIF_MI_XTD_FMT_CTRL_MP_CB_CR_SWAP;
		rkisp1_write(rkisp1, reg, RKISP1_CIF_MI_XTD_FORMAT_CTRL);
	}

	rkisp1_mi_config_ctrl(stream);

	reg = rkisp1_read(rkisp1, RKISP1_CIF_MI_CTRL);
	reg &= ~RKISP1_MI_CTRL_MP_FMT_MASK;
	reg |= stream->out_isp_fmt->write_format;
	rkisp1_write(rkisp1, reg, RKISP1_CIF_MI_CTRL);

	reg = rkisp1_read(rkisp1, RKISP1_CIF_MI_CTRL);
	reg |= RKISP1_CIF_MI_MP_AUTOUPDATE_ENABLE;
	rkisp1_write(rkisp1, reg, RKISP1_CIF_MI_CTRL);

	return 0;
}

/*
 * configure memory interface for selfpath
 * This should only be called when stream-on
 */
static int rkisp1_stream_sp_config(struct rkisp1_stream *stream)
{
	struct rkisp1_device *rkisp1 = stream->rkisp1;
	const struct rkisp1_stream_fmt *output_isp_fmt = stream->out_isp_fmt;
	const struct v4l2_pix_format_mplane *pixm = &stream->out_fmt;
	u32 mi_ctrl;

	rkisp1_write(rkisp1, rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_Y),
		     stream->config->mi.y_size_init);
	rkisp1_write(rkisp1, rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_CB),
		     stream->config->mi.cb_size_init);
	rkisp1_write(rkisp1, rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_CR),
		     stream->config->mi.cr_size_init);

	rkisp1_write(rkisp1, pixm->width, RKISP1_CIF_MI_SP_Y_PIC_WIDTH);
	rkisp1_write(rkisp1, pixm->height, RKISP1_CIF_MI_SP_Y_PIC_HEIGHT);
	rkisp1_write(rkisp1, stream->u.sp.y_stride, RKISP1_CIF_MI_SP_Y_LLENGTH);

	rkisp1_irq_frame_end_enable(stream);
	if (output_isp_fmt->uv_swap) {
		u32 reg = rkisp1_read(rkisp1, RKISP1_CIF_MI_XTD_FORMAT_CTRL);

		rkisp1_write(rkisp1, reg & ~BIT(1), RKISP1_CIF_MI_XTD_FORMAT_CTRL);
	}

	rkisp1_mi_config_ctrl(stream);

	mi_ctrl = rkisp1_read(rkisp1, RKISP1_CIF_MI_CTRL);
	mi_ctrl &= ~RKISP1_MI_CTRL_SP_FMT_MASK;
	mi_ctrl |= stream->out_isp_fmt->write_format |
		   RKISP1_SP_IN_FMT |
		   output_isp_fmt->output_format |
		   RKISP1_CIF_MI_SP_AUTOUPDATE_ENABLE;
	rkisp1_write(rkisp1, mi_ctrl, RKISP1_CIF_MI_CTRL);
	return 0;
}

static void rkisp1_stream_mp_disable(struct rkisp1_stream *stream)
{
	u32 mi_ctrl = rkisp1_read(stream->rkisp1, RKISP1_CIF_MI_CTRL);

	mi_ctrl &= ~(RKISP1_CIF_MI_CTRL_MP_ENABLE |
		     RKISP1_CIF_MI_CTRL_RAW_ENABLE);
	rkisp1_write(stream->rkisp1, mi_ctrl, RKISP1_CIF_MI_CTRL);
}

static void rkisp1_stream_sp_disable(struct rkisp1_stream *stream)
{
	u32 mi_ctrl = rkisp1_read(stream->rkisp1, RKISP1_CIF_MI_CTRL);

	mi_ctrl &= ~RKISP1_CIF_MI_CTRL_SP_ENABLE;
	rkisp1_write(stream->rkisp1, mi_ctrl, RKISP1_CIF_MI_CTRL);
}

static void rkisp1_stream_mp_enable(struct rkisp1_stream *stream)
{
	const struct rkisp1_stream_fmt *isp_fmt = stream->out_isp_fmt;
	u32 mi_ctrl;

	rkisp1_stream_mp_disable(stream);

	mi_ctrl = rkisp1_read(stream->rkisp1, RKISP1_CIF_MI_CTRL);
	if (isp_fmt->fmt_type == RKISP1_FMT_BAYER)
		mi_ctrl |= RKISP1_CIF_MI_CTRL_RAW_ENABLE;
	/* YUV */
	else
		mi_ctrl |= RKISP1_CIF_MI_CTRL_MP_ENABLE;

	rkisp1_write(stream->rkisp1, mi_ctrl, RKISP1_CIF_MI_CTRL);
}

static void rkisp1_stream_sp_enable(struct rkisp1_stream *stream)
{
	u32 mi_ctrl = rkisp1_read(stream->rkisp1, RKISP1_CIF_MI_CTRL);

	mi_ctrl |= RKISP1_CIF_MI_CTRL_SP_ENABLE;
	rkisp1_write(stream->rkisp1, mi_ctrl, RKISP1_CIF_MI_CTRL);
}

static void rkisp1_stream_mp_sp_stop(struct rkisp1_stream *stream)
{
	if (!stream->streaming)
		return;
	rkisp1_write(stream->rkisp1,
		     RKISP1_CIF_MI_FRAME(stream), RKISP1_CIF_MI_ICR);
	stream->ops->disable(stream);
}

static bool rkisp1_stream_mp_is_stopped(struct rkisp1_stream *stream)
{
	u32 en = RKISP1_CIF_MI_CTRL_SHD_MP_IN_ENABLED |
		 RKISP1_CIF_MI_CTRL_SHD_RAW_OUT_ENABLED;

	return !(rkisp1_read(stream->rkisp1, RKISP1_CIF_MI_CTRL_SHD) & en);
}

static bool rkisp1_stream_sp_is_stopped(struct rkisp1_stream *stream)
{
	return !(rkisp1_read(stream->rkisp1, RKISP1_CIF_MI_CTRL_SHD) &
		 RKISP1_CIF_MI_CTRL_SHD_SP_IN_ENABLED);
}

static void rkisp1_stream_mp_set_data_path(struct rkisp1_stream *stream)
{
	u32 dpcl = rkisp1_read(stream->rkisp1, RKISP1_CIF_VI_DPCL);

	dpcl = dpcl | RKISP1_CIF_VI_DPCL_CHAN_MODE_MP |
	       RKISP1_CIF_VI_DPCL_MP_MUX_MRSZ_MI;
	rkisp1_write(stream->rkisp1, dpcl, RKISP1_CIF_VI_DPCL);
}

static void rkisp1_stream_sp_set_data_path(struct rkisp1_stream *stream)
{
	u32 dpcl = rkisp1_read(stream->rkisp1, RKISP1_CIF_VI_DPCL);

	dpcl |= RKISP1_CIF_VI_DPCL_CHAN_MODE_SP;
	rkisp1_write(stream->rkisp1, dpcl, RKISP1_CIF_VI_DPCL);
}

static struct rkisp1_streams_ops rkisp1_mp_streams_ops = {
	.config = rkisp1_stream_mp_config,
	.enable = rkisp1_stream_mp_enable,
	.disable = rkisp1_stream_mp_disable,
	.stop = rkisp1_stream_mp_sp_stop,
	.set_data_path = rkisp1_stream_mp_set_data_path,
	.is_stopped = rkisp1_stream_mp_is_stopped,
};

static struct rkisp1_streams_ops rkisp1_sp_streams_ops = {
	.config = rkisp1_stream_sp_config,
	.enable = rkisp1_stream_sp_enable,
	.disable = rkisp1_stream_sp_disable,
	.stop = rkisp1_stream_mp_sp_stop,
	.set_data_path = rkisp1_stream_sp_set_data_path,
	.is_stopped = rkisp1_stream_sp_is_stopped,
};

/* ----------------------------------------------------------------------------
 * Frame buffer operations
 */

static int rkisp1_dummy_buf_create(struct rkisp1_stream *stream)
{
	const struct v4l2_pix_format_mplane *pixm = &stream->out_fmt;
	struct rkisp1_dummy_buffer *dummy_buf = &stream->dummy_buf;

	dummy_buf->size = max3(rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_Y),
			       rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_CB),
			       rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_CR));

	/* The driver never acess vaddr, no mapping is required */
	dummy_buf->vaddr = dma_alloc_attrs(stream->rkisp1->dev,
					   dummy_buf->size,
					   &dummy_buf->dma_addr,
					   GFP_KERNEL,
					   DMA_ATTR_NO_KERNEL_MAPPING);
	if (!dummy_buf->vaddr) {
		dev_err(stream->rkisp1->dev,
			"Failed to allocate the memory for dummy buffer\n");
		return -ENOMEM;
	}

	return 0;
}

static void rkisp1_dummy_buf_destroy(struct rkisp1_stream *stream)
{
	struct rkisp1_dummy_buffer *dummy_buf = &stream->dummy_buf;

	dma_free_attrs(stream->rkisp1->dev, dummy_buf->size, dummy_buf->vaddr,
		       dummy_buf->dma_addr, DMA_ATTR_NO_KERNEL_MAPPING);
}

/*
 * Update buffer info to memory interface. Called in interrupt
 * context by rkisp1_set_next_buf(), and in process context by vb2_ops.buf_queue().
 */
static void rkisp1_set_next_buf_regs(struct rkisp1_stream *stream)
{
	struct rkisp1_dummy_buffer *dummy_buf = &stream->dummy_buf;

	/*
	 * The dummy space allocated by dma_alloc_coherent is used, we can
	 * throw data to it if there is no available buffer.
	 */
	if (stream->next_buf) {
		u32 *buff_addr = stream->next_buf->buff_addr;

		rkisp1_write(stream->rkisp1,
			     buff_addr[RKISP1_PLANE_Y],
			     stream->config->mi.y_base_ad_init);
		rkisp1_write(stream->rkisp1,
			     buff_addr[RKISP1_PLANE_CB],
			     stream->config->mi.cb_base_ad_init);
		rkisp1_write(stream->rkisp1,
			     buff_addr[RKISP1_PLANE_CR],
			     stream->config->mi.cr_base_ad_init);
	} else {
		dev_dbg(stream->rkisp1->dev,
			"stream %d: to dummy buf\n", stream->id);
		rkisp1_write(stream->rkisp1,
			     dummy_buf->dma_addr,
			     stream->config->mi.y_base_ad_init);
		rkisp1_write(stream->rkisp1,
			     dummy_buf->dma_addr,
			     stream->config->mi.cb_base_ad_init);
		rkisp1_write(stream->rkisp1,
			     dummy_buf->dma_addr,
			     stream->config->mi.cr_base_ad_init);
	}

	/* Set plane offsets */
	rkisp1_write(stream->rkisp1, 0, stream->config->mi.y_offs_cnt_init);
	rkisp1_write(stream->rkisp1, 0, stream->config->mi.cb_offs_cnt_init);
	rkisp1_write(stream->rkisp1, 0, stream->config->mi.cr_offs_cnt_init);
}

/*
 * This function is called when a frame end comes. The next frame
 * is processing and we should set up buffer for next-next frame,
 * otherwise it will overflow.
 */
static int rkisp1_set_next_buf(struct rkisp1_stream *stream)
{
	const struct v4l2_pix_format_mplane *pixm = &stream->out_fmt;
	struct rkisp1_isp_subdev *isp_sd = &stream->rkisp1->isp_sdev;
	struct rkisp1_buffer *curr_buf = stream->curr_buf;
	unsigned long lock_flags = 0;
	unsigned int i;

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);

	if (curr_buf) {
		/* Dequeue a filled buffer */
		for (i = 0; i < pixm->num_planes; i++) {
			u32 payload_size =
				stream->out_fmt.plane_fmt[i].sizeimage;

			vb2_set_plane_payload(&curr_buf->vb.vb2_buf, i,
					      payload_size);
		}
		curr_buf->vb.sequence = atomic_read(&isp_sd->frm_sync_seq) - 1;
		curr_buf->vb.vb2_buf.timestamp = ktime_get_boottime_ns();
		curr_buf->vb.field = V4L2_FIELD_NONE;
		vb2_buffer_done(&curr_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}

	/* Next frame is writing to it */
	stream->curr_buf = stream->next_buf;
	stream->next_buf = NULL;

	/* Setup an empty buffer for the next-next frame */
	if (!list_empty(&stream->buf_queue)) {
		stream->next_buf = list_first_entry(&stream->buf_queue,
						    struct rkisp1_buffer,
						    queue);
		list_del(&stream->next_buf->queue);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);

	rkisp1_set_next_buf_regs(stream);

	return 0;
}

void rkisp1_stream_isr_thread(struct rkisp1_device *rkisp1)
{
	unsigned long lock_flags = 0;
	unsigned int i;
	u32 status;

	spin_lock_irqsave(&rkisp1->irq_status_lock, lock_flags);
	status = rkisp1->irq_status_mi;
	spin_unlock_irqrestore(&rkisp1->irq_status_lock, lock_flags);

	for (i = 0; i < ARRAY_SIZE(rkisp1->streams); ++i) {
		struct rkisp1_stream *stream = &rkisp1->streams[i];

		if (!(status & RKISP1_CIF_MI_FRAME(stream)))
			continue;
		if (!stream->stopping) {
			rkisp1_set_next_buf(stream);
			continue;
		}
		/*
		 * Make sure stream is actually stopped, whose state
		 * can be read from the shadow register, before
		 * wake_up() thread which would immediately free all
		 * frame buffers. stop() takes effect at the next
		 * frame end that sync the configurations to shadow
		 * regs.
		 */
		if (!stream->ops->is_stopped(stream)) {
			stream->ops->stop(stream);
			continue;
		}
		stream->stopping = false;
		stream->streaming = false;
		// TODO Use thread IRQ?
		wake_up(&stream->done);
	}
}

/* ----------------------------------------------------------------------------
 * Vb2 operations
 */

static int rkisp1_vb2_queue_setup(struct vb2_queue *queue,
			      unsigned int *num_buffers,
			      unsigned int *num_planes,
			      unsigned int sizes[],
			      struct device *alloc_devs[])
{
	struct rkisp1_stream *stream = queue->drv_priv;
	const struct v4l2_pix_format_mplane *pixm = &stream->out_fmt;
	unsigned int i;

	if (*num_planes) {
		if (*num_planes != pixm->num_planes)
			return -EINVAL;

		for (i = 0; i < pixm->num_planes; i++)
			if (sizes[i] < pixm->plane_fmt[i].sizeimage)
				return -EINVAL;
	} else {
		*num_planes = pixm->num_planes;
		for (i = 0; i < stream->out_fmt.num_planes; i++)
			sizes[i] = pixm->plane_fmt[i].sizeimage;
	}

	dev_dbg(stream->rkisp1->dev, "%s count %d, size %d\n",
		v4l2_type_names[queue->type], *num_buffers, sizes[0]);

	return 0;
}

/*
 * The vb2_buffer are stored in rkisp1_buffer, in order to unify
 * mplane buffer and none-mplane buffer.
 */
static void rkisp1_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp1_buffer *ispbuf = rkisp1_to_rkisp1_buffer(vbuf);
	struct rkisp1_stream *stream = vb->vb2_queue->drv_priv;
	const struct v4l2_pix_format_mplane *pixm = &stream->out_fmt;
	unsigned long lock_flags = 0;
	unsigned int i;

	memset(ispbuf->buff_addr, 0, sizeof(ispbuf->buff_addr));
	for (i = 0; i < pixm->num_planes; i++)
		ispbuf->buff_addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);

	/* Convert to non-MPLANE */
	if (pixm->num_planes == 1) {
		ispbuf->buff_addr[RKISP1_PLANE_CB] =
			ispbuf->buff_addr[RKISP1_PLANE_Y] +
			rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_Y);
		ispbuf->buff_addr[RKISP1_PLANE_CR] =
			ispbuf->buff_addr[RKISP1_PLANE_CB] +
			rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_CB);
	}

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);

	/*
	 * If there's no next buffer assigned, queue this buffer directly
	 * as the next buffer, and update the memory interface.
	 */
	if (stream->streaming && !stream->next_buf &&
	    atomic_read(&stream->rkisp1->isp_sdev.frm_sync_seq) == 0) {
		stream->next_buf = ispbuf;
		rkisp1_set_next_buf_regs(stream);
	} else {
		list_add_tail(&ispbuf->queue, &stream->buf_queue);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
}

static int rkisp1_vb2_buf_prepare(struct vb2_buffer *vb)
{
	struct rkisp1_stream *stream = vb->vb2_queue->drv_priv;
	unsigned int i;

	for (i = 0; i < stream->out_fmt.num_planes; i++) {
		unsigned long size = stream->out_fmt.plane_fmt[i].sizeimage;

		if (vb2_plane_size(vb, i) < size) {
			dev_err(stream->rkisp1->dev,
				"User buffer too small (%ld < %ld)\n",
				vb2_plane_size(vb, i), size);
			return -EINVAL;
		}
		vb2_set_plane_payload(vb, i, size);
	}

	return 0;
}

static void rkisp1_return_all_buffers(struct rkisp1_stream *stream,
				      enum vb2_buffer_state state)
{
	unsigned long lock_flags = 0;
	struct rkisp1_buffer *buf;

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	if (stream->curr_buf) {
		list_add_tail(&stream->curr_buf->queue, &stream->buf_queue);
		stream->curr_buf = NULL;
	}
	if (stream->next_buf) {
		list_add_tail(&stream->next_buf->queue, &stream->buf_queue);
		stream->next_buf = NULL;
	}
	while (!list_empty(&stream->buf_queue)) {
		buf = list_first_entry(&stream->buf_queue,
				       struct rkisp1_buffer, queue);
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
}

/*
 * rkisp1_pipeline_sink_walk - Walk through the pipeline and call cb
 * @from: entity at which to start pipeline walk
 * @until: entity at which to stop pipeline walk
 *
 * Walk the entities chain starting at the pipeline video node and stop
 * all subdevices in the chain.
 *
 * If the until argument isn't NULL, stop the pipeline walk when reaching the
 * until entity. This is used to disable a partially started pipeline due to a
 * subdev start error.
 */
static int rkisp1_pipeline_sink_walk(struct media_entity *from,
				     struct media_entity *until,
				     int (*cb)(struct media_entity *from,
					       struct media_entity *curr))
{
	struct media_entity *entity = from;
	struct media_pad *pad;
	unsigned int i;
	int ret;

	// TODO while (1) are dangerous
	while (1) {
		pad = NULL;
		/* Find remote source pad */
		for (i = 0; i < entity->num_pads; i++) {
			struct media_pad *spad = &entity->pads[i];

			if (!(spad->flags & MEDIA_PAD_FL_SINK))
				continue;
			pad = media_entity_remote_pad(spad);
			if (pad && is_media_entity_v4l2_subdev(pad->entity))
				break;
		}
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			break;

		entity = pad->entity;
		if (entity == until)
			break;

		ret = cb(from, entity);
		if (ret)
			return ret;
	}

	return 0;
}

static int rkisp1_pipeline_disable_cb(struct media_entity *from,
				      struct media_entity *curr)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(curr);
	int ret;

	ret = v4l2_subdev_call(sd, video, s_stream, false);
	if (ret) {
		dev_err(sd->dev, "%s: could not disable stream.\n", sd->name);
		return ret;
	}
	return 0;
}

// TODO: this is a core change.
static int rkisp1_pipeline_enable_cb(struct media_entity *from,
				     struct media_entity *curr)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(curr);
	int ret;

	ret = v4l2_subdev_call(sd, video, s_stream, true);
	if (ret) {
		dev_err(sd->dev, "%s: could not enable stream.\n", sd->name);
		rkisp1_pipeline_sink_walk(from, curr,
					  rkisp1_pipeline_disable_cb);
		return ret;
	}
	return 0;
}

/*
 * Set flags and wait, it should stop in interrupt.
 * If it didn't, stop it by force.
 */
static void rkisp1_stream_stop(struct rkisp1_stream *stream)
{
	struct rkisp1_device *rkisp1 = stream->rkisp1;
	int ret;

	stream->stopping = true;
	ret = wait_event_timeout(stream->done,
				 !stream->streaming,
				 msecs_to_jiffies(1000));
	if (!ret) {
		dev_warn(rkisp1->dev, "waiting on event return error %d\n", ret);
		stream->ops->stop(stream);
		stream->stopping = false;
		stream->streaming = false;
	}
	rkisp1_dcrop_disable(stream, RKISP1_SHADOW_REGS_ASYNC);
	rkisp1_rsz_disable(stream, RKISP1_SHADOW_REGS_ASYNC);
}

static void rkisp1_vb2_stop_streaming(struct vb2_queue *queue)
{
	struct rkisp1_stream *stream = queue->drv_priv;
	struct rkisp1_vdev_node *node = &stream->vnode;
	struct rkisp1_device *rkisp1 = stream->rkisp1;
	int ret;

	rkisp1_stream_stop(stream);
	media_pipeline_stop(&node->vdev.entity);
	ret = rkisp1_pipeline_sink_walk(&node->vdev.entity, NULL,
					rkisp1_pipeline_disable_cb);
	if (ret)
		dev_err(rkisp1->dev, "pipeline stream-off failed error:%d\n", ret);

	rkisp1_return_all_buffers(stream, VB2_BUF_STATE_ERROR);

	ret = pm_runtime_put(rkisp1->dev);
	if (ret)
		dev_err(rkisp1->dev, "power down failed error:%d\n", ret);

	ret = v4l2_pipeline_pm_use(&node->vdev.entity, 0);
	if (ret)
		dev_err(rkisp1->dev, "pipeline close failed error:%d\n", ret);

	// TODO: can be moved to the allocation ioctls. this has an impact,
	// of course.
	// EDIT: not true! vb2 doesn't allow this naturally, the API
	// doesn't allow to alloc bounce buffers, and I don't want to
	// be the one extending it!
	// There are two ioctl for allocation: CREATE_BUF and REQUEST_BUF,
	// we'd have to hook both - which is a pita.
	// Also, many drivers (stk1160, coda and hantro) already allocate
	// auxiliary buffers in start_streaming.
	// If fragmentation is a concern, see 'keep_buffers' parameter
	// in stk1160.
	rkisp1_dummy_buf_destroy(stream);
}

/*
 * Most of registers inside rockchip ISP1 have shadow register since
 * they must be not changed during processing a frame.
 * Usually, each sub-module updates its shadow register after
 * processing the last pixel of a frame.
 */
static int rkisp1_stream_start(struct rkisp1_stream *stream)
{
	struct rkisp1_device *rkisp1 = stream->rkisp1;
	struct rkisp1_stream *other = &rkisp1->streams[stream->id ^ 1];
	enum rkisp1_shadow_regs_when when = RKISP1_SHADOW_REGS_SYNC;
	int ret;

	if (other->streaming)
		when = RKISP1_SHADOW_REGS_ASYNC;

	ret = rkisp1_rsz_config(stream, when);
	if (ret) {
		dev_err(rkisp1->dev, "config rsz failed with error %d\n", ret);
		return ret;
	}

	ret = rkisp1_dcrop_config(stream);
	if (ret) {
		dev_err(rkisp1->dev, "config dcrop failed with error %d\n", ret);
		return ret;
	}

	stream->ops->set_data_path(stream);
	ret = stream->ops->config(stream);
	if (ret)
		return ret;

	/* Setup a buffer for the next frame */
	rkisp1_set_next_buf(stream);
	stream->ops->enable(stream);
	/* It's safe to config ACTIVE and SHADOW regs for the
	 * first stream. While when the second is starting, do NOT
	 * force update because it also update the first one.
	 *
	 * The latter case would drop one more buf(that is 2) since
	 * there's not buf in shadow when the second FE received. This's
	 * also required because the second FE maybe corrupt especially
	 * when run at 120fps.
	 */
	if (!other->streaming) {
		/* force cfg update */
		rkisp1_write(rkisp1,
			     RKISP1_CIF_MI_INIT_SOFT_UPD, RKISP1_CIF_MI_INIT);
		rkisp1_set_next_buf(stream);
	}
	stream->streaming = true;

	return 0;
}

static int
rkisp1_vb2_start_streaming(struct vb2_queue *queue, unsigned int count)
{
	struct rkisp1_stream *stream = queue->drv_priv;
	struct media_entity *entity = &stream->vnode.vdev.entity;
	int ret;

	ret = rkisp1_dummy_buf_create(stream);
	if (ret)
		goto return_queued_buf;

	ret = pm_runtime_get_sync(stream->rkisp1->dev);
	if (ret) {
		dev_err(stream->rkisp1->dev, "power up failed %d\n", ret);
		goto destroy_dummy_buf;
	}
	ret = v4l2_pipeline_pm_use(entity, 1);
	if (ret) {
		dev_err(stream->rkisp1->dev, "open cif pipeline failed %d\n", ret);
		goto close_pipe;
	}
	// TODO: to be symmetric --to be safe in case there's
	// any correlation between pm_runtime and mc-pm--
	// the get order should be reversed to the put order.

	/* configure stream hardware to start */
	ret = rkisp1_stream_start(stream);
	if (ret) {
		dev_err(stream->rkisp1->dev, "start streaming failed\n");
		goto power_down;
	}

	/* start sub-devices */
	ret = rkisp1_pipeline_sink_walk(entity, NULL,
					rkisp1_pipeline_enable_cb);
	if (ret)
		goto stop_stream;

	ret = media_pipeline_start(entity, &stream->rkisp1->pipe);
	if (ret) {
		dev_err(stream->rkisp1->dev, "start pipeline failed %d\n", ret);
		goto pipe_stream_off;
	}

	return 0;

pipe_stream_off:
	rkisp1_pipeline_sink_walk(entity, NULL, rkisp1_pipeline_disable_cb);
stop_stream:
	rkisp1_stream_stop(stream);
power_down:
	pm_runtime_put(stream->rkisp1->dev);
close_pipe:
	v4l2_pipeline_pm_use(entity, 0);
destroy_dummy_buf:
	rkisp1_dummy_buf_destroy(stream);
return_queued_buf:
	rkisp1_return_all_buffers(stream, VB2_BUF_STATE_QUEUED);

	return ret;
}

static struct vb2_ops rkisp1_vb2_ops = {
	.queue_setup = rkisp1_vb2_queue_setup,
	.buf_queue = rkisp1_vb2_buf_queue,
	.buf_prepare = rkisp1_vb2_buf_prepare,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.stop_streaming = rkisp1_vb2_stop_streaming,
	.start_streaming = rkisp1_vb2_start_streaming,
};

/* ----------------------------------------------------------------------------
 * IOCTLs operations
 */

/* TODO: check how we can integrate with v4l2_fill_pixfmt_mp() */
static void
rkisp1_fill_pixfmt(struct v4l2_pix_format_mplane *pixm, int stream_id)
{
	struct v4l2_plane_pix_format *plane_y = &pixm->plane_fmt[0];
	const struct v4l2_format_info *info;
	unsigned int i;
	u32 stride;

	info = v4l2_format_info(pixm->pixelformat);
	BUG_ON(!info);

	/*
	 * TODO: fill out info->block_w and info->block_h and use
	 * v4l2_format_block_width() and v4l2_format_block_height() to adjust
	 * alignment (see v4l2_fill_pixfmt_mp()).
	 * For Y component the line length in 4:2:x planar mode must be a
	 * multiple of 8, for all other component modes a multiple of 4 and
	 * for RGB 565 a multiple of 2. There are no restrictions for RGB
	 * 888/666.
	 */

	pixm->num_planes = info->mem_planes;
	stride = info->bpp[0] * pixm->width;
	/* Self path supports custom stride but Main path doesn't */
	if (stream_id == RKISP1_STREAM_MP || plane_y->bytesperline < stride )
		plane_y->bytesperline = stride;
	plane_y->sizeimage = plane_y->bytesperline * pixm->height;

	/* normalize stride to pixels per line */
	stride = DIV_ROUND_UP(plane_y->bytesperline, info->bpp[0]);

	for (i = 1; i < info->comp_planes; i++) {
		struct v4l2_plane_pix_format *plane = &pixm->plane_fmt[i];

		/* bytesperline for other components derive from Y component */
		plane->bytesperline = DIV_ROUND_UP(stride, info->hdiv) *
				      info->bpp[i];
		plane->sizeimage = plane->bytesperline *
				   DIV_ROUND_UP(pixm->height, info->vdiv);
	}

	/*
	 * If pixfmt is packed, then plane_fmt[0] should contain the total size
	 * considering all components. plane_fmt[i] for i > 0 should be ignored
	 * by userspace as mem_planes == 1, but we are keeping information there
	 * for convenience.
	 */
	if (info->mem_planes == 1)
		for (i = 1; i < info->comp_planes; i++)
			plane_y->sizeimage += pixm->plane_fmt[i].sizeimage;
}

static const struct rkisp1_stream_fmt *
rkisp1_find_fmt(const struct rkisp1_stream *stream, const u32 pixelfmt)
{
	unsigned int i;

	for (i = 0; i < stream->config->fmt_size; i++) {
		if (stream->config->fmts[i].fourcc == pixelfmt)
			return &stream->config->fmts[i];
	}
	return NULL;
}

static const struct rkisp1_stream_fmt *
rkisp1_try_fmt(const struct rkisp1_stream *stream,
	       struct v4l2_pix_format_mplane *pixm)
{
	const struct rkisp1_stream_cfg *config = stream->config;
	struct rkisp1_stream *other_stream =
			&stream->rkisp1->streams[stream->id ^ 1];
	const struct rkisp1_stream_fmt *fmt;

	fmt = rkisp1_find_fmt(stream, pixm->pixelformat);
	if (!fmt)
		fmt = config->fmts;

	/* do checks on resolution */
	pixm->width = clamp_t(u32, pixm->width, config->min_rsz_width,
			      config->max_rsz_width);
	pixm->height = clamp_t(u32, pixm->height, config->min_rsz_height,
			       config->max_rsz_height);
	pixm->field = V4L2_FIELD_NONE;
	pixm->colorspace = V4L2_COLORSPACE_DEFAULT;
	pixm->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;

	rkisp1_fill_pixfmt(pixm, stream->id);

	/* can not change quantization when stream-on */
	// TODO: this checks the _other_ stream.
	if (other_stream->streaming)
		pixm->quantization = other_stream->out_fmt.quantization;
	/* output full range by default, take effect in isp_params */
	else if (!pixm->quantization ||
		 pixm->quantization > V4L2_QUANTIZATION_LIM_RANGE)
		pixm->quantization = V4L2_QUANTIZATION_FULL_RANGE;

	dev_dbg(stream->rkisp1->dev,
		"%s: stream: %d req(%d, %d) out(%d, %d)\n", __func__,
		stream->id, pixm->width, pixm->height,
		stream->out_fmt.width, stream->out_fmt.height);

	return fmt;
}

static void rkisp1_set_fmt(struct rkisp1_stream *stream,
			   struct v4l2_pix_format_mplane *pixm)
{
	const struct v4l2_format_info *pixfmt_info;

	stream->out_isp_fmt = rkisp1_try_fmt(stream, pixm);
	pixfmt_info = v4l2_format_info(pixm->pixelformat);
	stream->out_fmt = *pixm;

	/* SP supports custom stride in number of pixels of the Y plane */
	if (stream->id == RKISP1_STREAM_SP)
		stream->u.sp.y_stride = pixm->plane_fmt[0].bytesperline /
					pixfmt_info->bpp[0];
	else
		stream->u.mp.raw_enable =
			(stream->out_isp_fmt->fmt_type == RKISP1_FMT_BAYER);

	dev_dbg(stream->rkisp1->dev,
		"%s: stream: %d req(%d, %d) out(%d, %d)\n", __func__,
		stream->id, pixm->width, pixm->height,
		stream->out_fmt.width, stream->out_fmt.height);
}

static int rkisp1_try_fmt_vid_cap_mplane(struct file *file, void *fh,
					 struct v4l2_format *f)
{
	struct rkisp1_stream *stream = video_drvdata(file);

	rkisp1_try_fmt(stream, &f->fmt.pix_mp);

	return 0;
}

static int rkisp1_enum_fmt_vid_cap_mplane(struct file *file, void *priv,
					  struct v4l2_fmtdesc *f)
{
	struct rkisp1_stream *stream = video_drvdata(file);
	const struct rkisp1_stream_fmt *fmt = NULL;

	if (f->index >= stream->config->fmt_size)
		return -EINVAL;

	fmt = &stream->config->fmts[f->index];
	f->pixelformat = fmt->fourcc;

	return 0;
}

static int rkisp1_s_fmt_vid_cap_mplane(struct file *file,
				       void *priv, struct v4l2_format *f)
{
	struct rkisp1_stream *stream = video_drvdata(file);
	struct rkisp1_vdev_node *node =
				rkisp1_vdev_to_node(&stream->vnode.vdev);

	if (vb2_is_busy(&node->buf_queue))
		return -EBUSY;

	rkisp1_set_fmt(stream, &f->fmt.pix_mp);

	return 0;
}

static int rkisp1_g_fmt_vid_cap_mplane(struct file *file, void *fh,
				       struct v4l2_format *f)
{
	struct rkisp1_stream *stream = video_drvdata(file);

	f->fmt.pix_mp = stream->out_fmt;

	return 0;
}

static int rkisp1_g_selection(struct file *file, void *prv,
			      struct v4l2_selection *sel)
{
	struct rkisp1_stream *stream = video_drvdata(file);
	struct v4l2_rect *dcrop = &stream->dcrop;
	const struct v4l2_rect *input_win;

	if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	input_win = rkisp1_isp_sd_get_pad_crop(&stream->rkisp1->isp_sdev, NULL,
					       RKISP1_ISP_PAD_SINK_VIDEO,
					       V4L2_SUBDEV_FORMAT_ACTIVE);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.width = input_win->width;
		sel->r.height = input_win->height;
		sel->r.left = 0;
		sel->r.top = 0;
		break;
	case V4L2_SEL_TGT_CROP:
		sel->r = *dcrop;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static struct v4l2_rect *rkisp1_crop_adjust(struct rkisp1_stream *stream,
					    struct v4l2_rect *sel,
					    const struct v4l2_rect *in)
{
	/* Not crop for MP bayer raw data */
	if (stream->id == RKISP1_STREAM_MP &&
	    stream->out_isp_fmt->fmt_type == RKISP1_FMT_BAYER) {
		sel->left = 0;
		sel->top = 0;
		sel->width = in->width;
		sel->height = in->height;
		return sel;
	}

	/* TODO: use v4l2_rect_set_min_size() and v4l2_rect_map_inside() */
	sel->left = ALIGN(sel->left, 2);
	sel->width = ALIGN(sel->width, 2);
	sel->left = clamp_t(u32, sel->left, 0, in->width - RKISP1_IN_MIN_WIDTH);
	sel->top = clamp_t(u32, sel->top, 0,
			   in->height - RKISP1_IN_MIN_HEIGHT);
	sel->width = clamp_t(u32, sel->width,
			     RKISP1_IN_MIN_WIDTH, in->width - sel->left);
	sel->height = clamp_t(u32, sel->height,
			      RKISP1_IN_MIN_HEIGHT, in->height - sel->top);
	return sel;
}

static int
rkisp1_s_selection(struct file *file, void *prv, struct v4l2_selection *sel)
{
	struct rkisp1_stream *stream = video_drvdata(file);
	struct video_device *vdev = &stream->vnode.vdev;
	struct rkisp1_vdev_node *node = rkisp1_vdev_to_node(vdev);
	struct v4l2_rect *dcrop = &stream->dcrop;
	const struct v4l2_rect *input_win;

	if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	if (vb2_is_busy(&node->buf_queue))
		return -EBUSY;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	if (sel->flags != 0)
		return -EINVAL;

	input_win = rkisp1_isp_sd_get_pad_crop(&stream->rkisp1->isp_sdev, NULL,
					       RKISP1_ISP_PAD_SINK_VIDEO,
					       V4L2_SUBDEV_FORMAT_ACTIVE);

	if (sel->target == V4L2_SEL_TGT_CROP) {
		*dcrop = *rkisp1_crop_adjust(stream, &sel->r, input_win);
		dev_dbg(stream->rkisp1->dev,
			"stream %d crop(%d,%d)/%dx%d\n", stream->id,
			dcrop->left, dcrop->top, dcrop->width, dcrop->height);
	}

	return 0;
}

static int
rkisp1_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	struct rkisp1_stream *stream = video_drvdata(file);
	struct rkisp1_device *rkisp1 = stream->rkisp1;

	strscpy(cap->driver, rkisp1->dev->driver->name, sizeof(cap->driver));
	strscpy(cap->card, rkisp1->dev->driver->name, sizeof(cap->card));
	strscpy(cap->bus_info, "platform: " RKISP1_DRIVER_NAME,
		sizeof(cap->bus_info));

	return 0;
}

static const struct v4l2_ioctl_ops rkisp1_v4l2_ioctl_ops = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_try_fmt_vid_cap_mplane = rkisp1_try_fmt_vid_cap_mplane,
	.vidioc_s_fmt_vid_cap_mplane = rkisp1_s_fmt_vid_cap_mplane,
	.vidioc_g_fmt_vid_cap_mplane = rkisp1_g_fmt_vid_cap_mplane,
	.vidioc_enum_fmt_vid_cap = rkisp1_enum_fmt_vid_cap_mplane,
	.vidioc_s_selection = rkisp1_s_selection,
	.vidioc_g_selection = rkisp1_g_selection,
	.vidioc_querycap = rkisp1_querycap,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static int rkisp1_stream_link_validate(struct media_link *link)
{
	struct video_device *vdev =
		media_entity_to_video_device(link->sink->entity);
	struct rkisp1_stream *stream = video_get_drvdata(vdev);
	struct rkisp1_isp_subdev *isp_sd = &stream->rkisp1->isp_sdev;
	const struct v4l2_mbus_framefmt *ispsd_frm;
	u16 isp_quant, cap_quant;

	if (stream->out_isp_fmt->fmt_type != isp_sd->out_fmt->fmt_type) {
		dev_err(isp_sd->sd.dev,
			"format type mismatch in link '%s:%d->%s:%d'\n",
			link->source->entity->name, link->source->index,
			link->sink->entity->name, link->sink->index);
		return -EPIPE;
	}

	ispsd_frm = rkisp1_isp_sd_get_pad_fmt(isp_sd, NULL,
					      RKISP1_ISP_PAD_SINK_VIDEO,
					      V4L2_SUBDEV_FORMAT_ACTIVE);

	/*
	 * TODO: we are considering default quantization as full range. Check if
	 * we can do this or not.
	 */
	cap_quant = stream->out_fmt.quantization;
	isp_quant = ispsd_frm->quantization;

	if (cap_quant == V4L2_QUANTIZATION_DEFAULT)
		cap_quant = V4L2_QUANTIZATION_FULL_RANGE;
	if (isp_quant == V4L2_QUANTIZATION_DEFAULT)
		isp_quant = V4L2_QUANTIZATION_FULL_RANGE;
	if (cap_quant != isp_quant) {
		dev_err(isp_sd->sd.dev,
			"quantization mismatch in link '%s:%d->%s:%d'\n",
			link->source->entity->name, link->source->index,
			link->sink->entity->name, link->sink->index);
		return -EPIPE;
	}

	return 0;
}

/* ----------------------------------------------------------------------------
 * core functions
 */

static const struct media_entity_operations rkisp1_isp_vdev_media_ops = {
	.link_validate = rkisp1_stream_link_validate,
};

static const struct v4l2_file_operations rkisp1_fops = {
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

static void rkisp1_unregister_stream_vdev(struct rkisp1_stream *stream)
{
	media_entity_cleanup(&stream->vnode.vdev.entity);
	video_unregister_device(&stream->vnode.vdev);
}

void rkisp1_unregister_stream_vdevs(struct rkisp1_device *rkisp1)
{
	struct rkisp1_stream *mp_stream = &rkisp1->streams[RKISP1_STREAM_MP];
	struct rkisp1_stream *sp_stream = &rkisp1->streams[RKISP1_STREAM_SP];

	rkisp1_unregister_stream_vdev(mp_stream);
	rkisp1_unregister_stream_vdev(sp_stream);
}

static int rkisp1_register_stream_vdev(struct rkisp1_stream *stream)
{
	struct v4l2_device *v4l2_dev = &stream->rkisp1->v4l2_dev;
	struct video_device *vdev = &stream->vnode.vdev;
	struct rkisp1_vdev_node *node;
	struct vb2_queue *q;
	int ret;
	const char * const dev_names[] = {RKISP1_SP_VDEV_NAME,
					  RKISP1_MP_VDEV_NAME};

	// TODO: maybe this is more readable? i'm always wary of conditionals
	// where a more expressive form is possible.
	strscpy(vdev->name, dev_names[stream->id], sizeof(vdev->name));
	node = rkisp1_vdev_to_node(vdev);
	mutex_init(&node->vlock);

	vdev->ioctl_ops = &rkisp1_v4l2_ioctl_ops;
	vdev->release = video_device_release_empty;
	vdev->fops = &rkisp1_fops;
	vdev->minor = -1;
	vdev->v4l2_dev = v4l2_dev;
	vdev->lock = &node->vlock;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE |
			    V4L2_CAP_STREAMING;
	vdev->entity.ops = &rkisp1_isp_vdev_media_ops;
	video_set_drvdata(vdev, stream);
	vdev->vfl_dir = VFL_DIR_RX;
	node->pad.flags = MEDIA_PAD_FL_SINK;

	q = &node->buf_queue;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes = VB2_MMAP | VB2_DMABUF; // TODO: probably USER_PTR works as well?
	q->drv_priv = stream;
	q->ops = &rkisp1_vb2_ops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct rkisp1_buffer);
	q->min_buffers_needed = RKISP1_MIN_BUFFERS_NEEDED;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &node->vlock;
	q->dev = stream->rkisp1->dev;
	ret = vb2_queue_init(q);
	if (ret) {
		dev_err(stream->rkisp1->dev,
			"vb2 queue init failed (err=%d)\n", ret);
		return ret;
	}

	vdev->queue = q;

	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (ret) {
		dev_err(stream->rkisp1->dev,
			"video_register_device failed with error %d\n", ret);
		return ret;
	}
	v4l2_info(v4l2_dev, "registered %s as /dev/video%d\n", vdev->name,
		  vdev->num);

	ret = media_entity_pads_init(&vdev->entity, 1, &node->pad);
	if (ret) {
		video_unregister_device(vdev);
		return ret;
	}

	return 0;
}

int rkisp1_register_stream_vdevs(struct rkisp1_device *rkisp1)
{
	struct rkisp1_stream *stream;
	unsigned int i, j;
	int ret;

	for (i = 0; i < RKISP1_MAX_STREAM; i++) {
		stream = &rkisp1->streams[i];
		stream->rkisp1 = rkisp1;
		ret = rkisp1_register_stream_vdev(stream);
		if (ret)
			goto err;
	}

	return 0;
// XXX use meaningful label names
err:
	for (j = 0; j < i; j++) {
		stream = &rkisp1->streams[j];
		rkisp1_unregister_stream_vdev(stream);
	}

	return ret;
}

void rkisp1_stream_init(struct rkisp1_device *rkisp1, u32 id)
{
	struct rkisp1_stream *stream = &rkisp1->streams[id];
	struct v4l2_pix_format_mplane pixm;

	memset(stream, 0, sizeof(*stream));
	stream->id = id;
	stream->rkisp1 = rkisp1;

	INIT_LIST_HEAD(&stream->buf_queue);
	init_waitqueue_head(&stream->done);
	spin_lock_init(&stream->vbq_lock);
	if (stream->id == RKISP1_STREAM_SP) {
		stream->ops = &rkisp1_sp_streams_ops;
		stream->config = &rkisp1_sp_stream_config;
	} else {
		stream->ops = &rkisp1_mp_streams_ops;
		stream->config = &rkisp1_mp_stream_config;
	}

	stream->streaming = false;

	memset(&pixm, 0, sizeof(pixm));
	pixm.pixelformat = V4L2_PIX_FMT_YUYV;
	pixm.width = RKISP1_DEFAULT_WIDTH;
	pixm.height = RKISP1_DEFAULT_HEIGHT;
	rkisp1_set_fmt(stream, &pixm);

	stream->dcrop.left = 0;
	stream->dcrop.top = 0;
	stream->dcrop.width = RKISP1_DEFAULT_WIDTH;
	stream->dcrop.height = RKISP1_DEFAULT_HEIGHT;
}
