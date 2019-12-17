// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip ISP1 Driver - V4l resizer device
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#include "rkisp1-common.h"

#define RKISP1_RSZ_MP_OUT_MAX_WIDTH		4416
#define RKISP1_RSZ_MP_OUT_MAX_HEIGHT		3312
#define RKISP1_RSZ_SP_OUT_MAX_WIDTH		1920
#define RKISP1_RSZ_SP_OUT_MAX_HEIGHT		1920
#define RKISP1_RSZ_OUT_MIN_WIDTH		32
#define RKISP1_RSZ_OUT_MIN_HEIGHT		16

#define RKISP1_IN_MIN_WIDTH		RKISP1_ISP_MIN_WIDTH
#define RKISP1_IN_MIN_HEIGHT		RKISP1_ISP_MIN_HEIGHT
#define RKISP1_IN_MAX_WIDTH		RKISP1_ISP_MAX_WIDTH
#define RKISP1_IN_MAX_HEIGHT		RKISP1_ISP_MAX_HEIGHT

#define RKISP1_DEF_FMT MEDIA_BUS_FMT_YUYV8_2X8
#define RKISP1_DEF_FMT_TYPE RKISP1_FMT_YUV

enum rkisp1_shadow_regs_when {
	RKISP1_SHADOW_REGS_SYNC,
	RKISP1_SHADOW_REGS_ASYNC,
};

struct rkisp1_rsz_config {
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
};

static const struct rkisp1_rsz_config rkisp1_rsz_config_mp = {
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
};

static const struct rkisp1_rsz_config rkisp1_rsz_config_sp = {
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
};

static struct v4l2_mbus_framefmt *
rkisp1_rsz_get_pad_fmt(struct rkisp1_resizer *rsz,
		       struct v4l2_subdev_pad_config *cfg,
		       unsigned int pad, u32 which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&rsz->sd, cfg, pad);
	else
		return v4l2_subdev_get_try_format(&rsz->sd, rsz->pad_cfg, pad);
}

static struct v4l2_rect *rkisp1_rsz_get_pad_crop(struct rkisp1_resizer *rsz,
					  struct v4l2_subdev_pad_config *cfg,
					  unsigned int pad, u32 which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_crop(&rsz->sd, cfg, pad);
	else
		return v4l2_subdev_get_try_crop(&rsz->sd, rsz->pad_cfg, pad);
}

/* ----------------------------------------------------------------------------
 * Dual crop hw configs
 */

static void rkisp1_dcrop_disable(struct rkisp1_resizer *rsz,
				 enum rkisp1_shadow_regs_when when)
{
	u32 dc_ctrl = rkisp1_read(rsz->rkisp1, rsz->config->dual_crop.ctrl);
	u32 mask = ~(rsz->config->dual_crop.yuvmode_mask |
		     rsz->config->dual_crop.rawmode_mask);

	dc_ctrl &= mask;
	if (when == RKISP1_SHADOW_REGS_ASYNC)
		dc_ctrl |= RKISP1_CIF_DUAL_CROP_GEN_CFG_UPD;
	else
		dc_ctrl |= RKISP1_CIF_DUAL_CROP_CFG_UPD;
	rkisp1_write(rsz->rkisp1, dc_ctrl, rsz->config->dual_crop.ctrl);
}

/* configure dual-crop unit */
static void rkisp1_dcrop_config(struct rkisp1_resizer *rsz)
{
	struct rkisp1_device *rkisp1 = rsz->rkisp1;
	struct v4l2_mbus_framefmt *in_fmt;
	struct v4l2_rect *in_crop;
	u32 dc_ctrl;

	in_crop = rkisp1_rsz_get_pad_crop(rsz, NULL, RKISP1_RSZ_PAD_SINK,
					  V4L2_SUBDEV_FORMAT_ACTIVE);
	in_fmt = rkisp1_rsz_get_pad_fmt(rsz, NULL, RKISP1_RSZ_PAD_SINK,
					V4L2_SUBDEV_FORMAT_ACTIVE);

	if (in_crop->width == in_fmt->width &&
	    in_crop->height == in_fmt->height &&
	    in_crop->left == 0 && in_crop->top == 0) {
		rkisp1_dcrop_disable(rsz, RKISP1_SHADOW_REGS_SYNC);
		dev_dbg(rkisp1->dev, "capture %d crop disabled\n", rsz->id);
		return;
	}

	dc_ctrl = rkisp1_read(rkisp1, rsz->config->dual_crop.ctrl);
	rkisp1_write(rkisp1, in_crop->left, rsz->config->dual_crop.h_offset);
	rkisp1_write(rkisp1, in_crop->top, rsz->config->dual_crop.v_offset);
	rkisp1_write(rkisp1, in_crop->width, rsz->config->dual_crop.h_size);
	rkisp1_write(rkisp1, in_crop->height, rsz->config->dual_crop.v_size);
	/* TODO: this is a mask, shouldn't it be dc_ctrl & ~mask ?*/
	dc_ctrl |= rsz->config->dual_crop.yuvmode_mask;
	dc_ctrl |= RKISP1_CIF_DUAL_CROP_CFG_UPD;
	rkisp1_write(rkisp1, dc_ctrl, rsz->config->dual_crop.ctrl);

	dev_dbg(rkisp1->dev, "stream %d crop: %dx%d -> %dx%d\n", rsz->id,
		in_fmt->width, in_fmt->height, in_crop->width, in_crop->height);
}

/* ----------------------------------------------------------------------------
 * Resizer hw configs
 */

static void rkisp1_rsz_dump_regs(struct rkisp1_resizer *rsz)
{
	dev_dbg(rsz->rkisp1->dev,
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
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.ctrl),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.ctrl_shd),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.scale_hy),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.scale_hy_shd),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.scale_hcb),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.scale_hcb_shd),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.scale_hcr),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.scale_hcr_shd),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.scale_vy),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.scale_vy_shd),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.scale_vc),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.scale_vc_shd),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.phase_hy),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.phase_hy_shd),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.phase_hc),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.phase_hc_shd),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.phase_vy),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.phase_vy_shd),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.phase_vc),
		rkisp1_read(rsz->rkisp1, rsz->config->rsz.phase_vc_shd));
}

static void rkisp1_rsz_update_shadow(struct rkisp1_resizer *rsz,
				     enum rkisp1_shadow_regs_when when)
{
	u32 ctrl_cfg = rkisp1_read(rsz->rkisp1, rsz->config->rsz.ctrl);

	if (when == RKISP1_SHADOW_REGS_ASYNC)
		ctrl_cfg |= RKISP1_CIF_RSZ_CTRL_CFG_UPD_AUTO;
	else
		ctrl_cfg |= RKISP1_CIF_RSZ_CTRL_CFG_UPD;

	rkisp1_write(rsz->rkisp1, ctrl_cfg, rsz->config->rsz.ctrl);
}

static u32 rkisp1_rsz_calc_ratio(u32 len_in, u32 len_out)
{
	if (len_in < len_out)
		return ((len_in - 1) * RKISP1_CIF_RSZ_SCALER_FACTOR) /
		       (len_out - 1);

	return ((len_out - 1) * RKISP1_CIF_RSZ_SCALER_FACTOR) /
	       (len_in - 1) + 1;
}

static void rkisp1_rsz_disable(struct rkisp1_resizer *rsz,
			       enum rkisp1_shadow_regs_when when)
{
	rkisp1_write(rsz->rkisp1, 0, rsz->config->rsz.ctrl);

	if (when == RKISP1_SHADOW_REGS_SYNC)
		rkisp1_rsz_update_shadow(rsz, when);
}

static void rkisp1_rsz_config_regs(struct rkisp1_resizer *rsz,
				   struct v4l2_rect *in_y,
				   struct v4l2_rect *in_c,
				   struct v4l2_rect *out_y,
				   struct v4l2_rect *out_c,
				   enum rkisp1_shadow_regs_when when)
{
	struct rkisp1_device *rkisp1 = rsz->rkisp1;
	u32 ratio, rsz_ctrl = 0;
	unsigned int i;

	/* No phase offset */
	rkisp1_write(rkisp1, 0, rsz->config->rsz.phase_hy);
	rkisp1_write(rkisp1, 0, rsz->config->rsz.phase_hc);
	rkisp1_write(rkisp1, 0, rsz->config->rsz.phase_vy);
	rkisp1_write(rkisp1, 0, rsz->config->rsz.phase_vc);

	/* Linear interpolation */
	for (i = 0; i < 64; i++) {
		rkisp1_write(rkisp1, i, rsz->config->rsz.scale_lut_addr);
		rkisp1_write(rkisp1, i, rsz->config->rsz.scale_lut);
	}

	if (in_y->width != out_y->width) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_HY_ENABLE;
		if (in_y->width < out_y->width)
			rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_HY_UP;
		ratio = rkisp1_rsz_calc_ratio(in_y->width, out_y->width);
		rkisp1_write(rkisp1, ratio, rsz->config->rsz.scale_hy);
	}

	if (in_c->width != out_c->width) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_HC_ENABLE;
		if (in_c->width < out_c->width)
			rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_HC_UP;
		ratio = rkisp1_rsz_calc_ratio(in_c->width, out_c->width);
		rkisp1_write(rkisp1, ratio, rsz->config->rsz.scale_hcb);
		rkisp1_write(rkisp1, ratio, rsz->config->rsz.scale_hcr);
	}

	if (in_y->height != out_y->height) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_VY_ENABLE;
		if (in_y->height < out_y->height)
			rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_VY_UP;
		ratio = rkisp1_rsz_calc_ratio(in_y->height, out_y->height);
		rkisp1_write(rkisp1, ratio, rsz->config->rsz.scale_vy);
	}

	if (in_c->height != out_c->height) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_VC_ENABLE;
		if (in_c->height < out_c->height)
			rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_VC_UP;
		ratio = rkisp1_rsz_calc_ratio(in_c->height, out_c->height);
		rkisp1_write(rkisp1, ratio, rsz->config->rsz.scale_vc);
	}

	rkisp1_write(rkisp1, rsz_ctrl, rsz->config->rsz.ctrl);

	rkisp1_rsz_update_shadow(rsz, when);
}

static void rkisp1_rsz_config(struct rkisp1_resizer *rsz,
			      enum rkisp1_shadow_regs_when when)
{
	u8 hdiv = RKISP1_MBUS_FMT_HDIV, vdiv = RKISP1_MBUS_FMT_VDIV;
	struct v4l2_mbus_framefmt *out_fmt;
	struct v4l2_rect in_y, in_c, out_y, out_c;
	struct v4l2_rect *in_crop;

	in_crop = rkisp1_rsz_get_pad_crop(rsz, NULL, RKISP1_RSZ_PAD_SINK,
					  V4L2_SUBDEV_FORMAT_ACTIVE);
	out_fmt = rkisp1_rsz_get_pad_fmt(rsz, NULL, RKISP1_RSZ_PAD_SRC,
					 V4L2_SUBDEV_FORMAT_ACTIVE);

	if (rsz->fmt_type == RKISP1_FMT_BAYER) {
		rkisp1_rsz_disable(rsz, when);
		return;
	}

	in_y.width = in_crop->width;
	in_y.height = in_crop->height;
	out_y.width = out_fmt->width;
	out_y.height = out_fmt->height;

	in_c.width = in_y.width / RKISP1_MBUS_FMT_HDIV;
	in_c.height = in_y.height / RKISP1_MBUS_FMT_VDIV;

	if (rsz->fmt_type == RKISP1_FMT_YUV) {
		struct rkisp1_capture *cap =
			&rsz->rkisp1->capture_devs[rsz->id];
		const struct v4l2_format_info *pixfmt_info =
			v4l2_format_info(cap->pix.fmt.pixelformat);

		hdiv = pixfmt_info->hdiv;
		vdiv = pixfmt_info->vdiv;
	}
	out_c.width = out_y.width / hdiv;
	out_c.height = out_y.height / vdiv;

	/* TODO: why this doesn't check in_y out_y ? */
	if (in_c.width == out_c.width && in_c.height == out_c.height) {
		rkisp1_rsz_disable(rsz, when);
		return;
	}

	dev_dbg(rsz->rkisp1->dev, "stream %d rsz/scale: %dx%d -> %dx%d\n",
		rsz->id, in_crop->width, in_crop->height,
		out_fmt->width, out_fmt->height);
	dev_dbg(rsz->rkisp1->dev, "chroma scaling %dx%d -> %dx%d\n",
		in_c.width, in_c.height, out_c.width, out_c.height);

	/* set values in the hw */
	rkisp1_rsz_config_regs(rsz, &in_y, &in_c, &out_y, &out_c, when);

	rkisp1_rsz_dump_regs(rsz);
}

/* ----------------------------------------------------------------------------
 * Subdev pad operations
 */

static int rkisp1_rsz_enum_mbus_code(struct v4l2_subdev *sd,
				     struct v4l2_subdev_pad_config *cfg,
				     struct v4l2_subdev_mbus_code_enum *code)
{
	struct rkisp1_resizer *rsz = container_of(sd, struct rkisp1_resizer,
						  sd);
	struct v4l2_subdev_pad_config dummy_cfg;
	u32 pad = code->pad;
	int ret;

	/* supported mbus codes are the same in isp sink pad */
	code->pad = RKISP1_ISP_PAD_SINK_VIDEO;
	ret = v4l2_subdev_call(&rsz->rkisp1->isp.sd, pad, enum_mbus_code,
				&dummy_cfg, code);

	/* restore pad */
	code->pad = pad;
	return ret;
}

static int rkisp1_rsz_init_config(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg)
{
	struct v4l2_mbus_framefmt *mf_in, *mf_out;
	struct v4l2_rect *mf_in_crop;

	mf_in = v4l2_subdev_get_try_format(sd, cfg, RKISP1_RSZ_PAD_SRC);
	mf_in->width = RKISP1_DEFAULT_WIDTH;
	mf_in->height = RKISP1_DEFAULT_HEIGHT;
	mf_in->field = V4L2_FIELD_NONE;
	mf_in->code = RKISP1_DEF_FMT;

	mf_in_crop = v4l2_subdev_get_try_crop(sd, cfg, RKISP1_RSZ_PAD_SINK);
	mf_in_crop->width = RKISP1_DEFAULT_WIDTH;
	mf_in_crop->height = RKISP1_DEFAULT_HEIGHT;
	mf_in_crop->left = 0;
	mf_in_crop->top = 0;

	mf_out = v4l2_subdev_get_try_format(sd, cfg, RKISP1_RSZ_PAD_SINK);
	*mf_out = *mf_in;

	/* NOTE: there is no crop in the source pad, only in the sink */

	return 0;
}

static void rkisp1_rsz_set_out_fmt(struct rkisp1_resizer *rsz,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_mbus_framefmt *format,
				   unsigned int which)
{
	struct v4l2_mbus_framefmt *out_fmt;

	out_fmt = rkisp1_rsz_get_pad_fmt(rsz, cfg, RKISP1_RSZ_PAD_SRC, which);
	/*
	 * TODO: check if other fields besides width/height are
	 * also configurable. If yes, then accept them from userspace.
	 */
	out_fmt->width = clamp_t(u32, format->width,
				 rsz->config->min_rsz_width,
				 rsz->config->max_rsz_width);
	out_fmt->height = clamp_t(u32, format->height,
				  rsz->config->min_rsz_height,
				  rsz->config->max_rsz_height);

	*format = *out_fmt;
}

static void rkisp1_rsz_set_in_crop(struct rkisp1_resizer *rsz,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_rect *r, unsigned int which)
{
	const struct rkisp1_isp_mbus_info *mbus_info;
	struct v4l2_mbus_framefmt *in_fmt;
	struct v4l2_rect *in_crop;

	in_fmt = rkisp1_rsz_get_pad_fmt(rsz, cfg, RKISP1_RSZ_PAD_SINK, which);
	in_crop = rkisp1_rsz_get_pad_crop(rsz, cfg, RKISP1_RSZ_PAD_SINK, which);

	// TODO: should we set the value back to r?

	/* Not crop for MP bayer raw data */
	mbus_info = rkisp1_isp_mbus_info_get(in_fmt->code);

	if (rsz->id == RKISP1_MAINPATH &&
	    mbus_info->fmt_type == RKISP1_FMT_BAYER) {
		in_crop->left = 0;
		in_crop->top = 0;
		in_crop->width = in_fmt->width;
		in_crop->height = in_fmt->height;
		return;
	}

	in_crop->left = ALIGN(r->left, 2);
	in_crop->width = ALIGN(r->width, 2);
	in_crop->top = r->top;
	in_crop->height = r->height;

	/* TODO: use v4l2_rect_set_min_size() and v4l2_rect_map_inside() */
	in_crop->left = ALIGN(in_crop->left, 2);
	in_crop->width = ALIGN(in_crop->width, 2);
	in_crop->left = clamp_t(u32, in_crop->left, 0,
				in_fmt->width - RKISP1_IN_MIN_WIDTH);
	in_crop->top = clamp_t(u32, in_crop->top, 0,
			       in_fmt->height - RKISP1_IN_MIN_HEIGHT);
	in_crop->width = clamp_t(u32, in_crop->width,
				 RKISP1_IN_MIN_WIDTH,
				 in_fmt->width - in_crop->left);
	in_crop->height = clamp_t(u32, in_crop->height,
				  RKISP1_IN_MIN_HEIGHT,
				  in_fmt->height - in_crop->top);

	/* Update source format if needed */
	// TODO, check rsz restrictions, and if the output requires update
}

static void rkisp1_rsz_set_in_fmt(struct rkisp1_resizer *rsz,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_mbus_framefmt *format,
				  unsigned int which)
{
	const struct rkisp1_isp_mbus_info *mbus_info;
	struct v4l2_mbus_framefmt *in_fmt, *out_fmt;
	struct v4l2_rect *in_crop;

	in_fmt = rkisp1_rsz_get_pad_fmt(rsz, cfg, RKISP1_RSZ_PAD_SINK, which);
	out_fmt = rkisp1_rsz_get_pad_fmt(rsz, cfg, RKISP1_RSZ_PAD_SRC, which);
	in_crop = rkisp1_rsz_get_pad_crop(rsz, cfg, RKISP1_RSZ_PAD_SINK, which);

	/*
	 * TODO: check if other fields besides width/height/quantization are
	 * also configurable. If yes, then accept them from userspace.
	 */
	in_fmt->code = format->code;
	mbus_info = rkisp1_isp_mbus_info_get(in_fmt->code);
	if (!mbus_info) {
		in_fmt->code = RKISP1_DEF_FMT;
		mbus_info = rkisp1_isp_mbus_info_get(in_fmt->code);
	}
	if (which == V4L2_SUBDEV_FORMAT_ACTIVE)
		rsz->fmt_type = mbus_info->fmt_type;

	if (rsz->id == RKISP1_MAINPATH &&
	    mbus_info->fmt_type == RKISP1_FMT_BAYER) {
		in_crop->left = 0;
		in_crop->top = 0;
		in_crop->width = in_fmt->width;
		in_crop->height = in_fmt->height;
		return;
	}

	/* Propagete to source pad */
	out_fmt->code = in_fmt->code;

	// TODO: check input restrictions
	in_fmt->width = clamp_t(u32, format->width,
				rsz->config->min_rsz_width,
				rsz->config->max_rsz_width);
	in_fmt->height = clamp_t(u32, format->height,
				 rsz->config->min_rsz_height,
				 rsz->config->max_rsz_height);

	*format = *in_fmt;

	/* Update sink crop */
	rkisp1_rsz_set_in_crop(rsz, cfg, in_crop, which);
}

static int rkisp1_rsz_get_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	struct rkisp1_resizer *rsz = container_of(sd, struct rkisp1_resizer,
						  sd);

	fmt->format = *rkisp1_rsz_get_pad_fmt(rsz, cfg, fmt->pad, fmt->which);
	return 0;
}

static int rkisp1_rsz_set_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	struct rkisp1_resizer *rsz = container_of(sd, struct rkisp1_resizer,
						  sd);

	if (fmt->pad == RKISP1_RSZ_PAD_SINK)
		rkisp1_rsz_set_in_fmt(rsz, cfg, &fmt->format, fmt->which);
	else
		rkisp1_rsz_set_out_fmt(rsz, cfg, &fmt->format, fmt->which);

	return 0;
}

static int rkisp1_rsz_get_selection(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_selection *sel)
{
	struct rkisp1_resizer *rsz = container_of(sd, struct rkisp1_resizer,
						  sd);
	struct v4l2_mbus_framefmt *mf_in;


	if (sel->pad == RKISP1_RSZ_PAD_SRC)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		mf_in = rkisp1_rsz_get_pad_fmt(rsz, cfg, RKISP1_RSZ_PAD_SINK,
					       sel->which);
		sel->r.height = mf_in->height;
		sel->r.width = mf_in->width;
		sel->r.left = 0;
		sel->r.top = 0;
		break;
	case V4L2_SEL_TGT_CROP:
		sel->r = *rkisp1_rsz_get_pad_crop(rsz, cfg, RKISP1_RSZ_PAD_SINK,
						  sel->which);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rkisp1_rsz_set_selection(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_selection *sel)
{
	struct rkisp1_resizer *rsz = container_of(sd, struct rkisp1_resizer,
						  sd);

	if (sel->target != V4L2_SEL_TGT_CROP || sel->pad == RKISP1_RSZ_PAD_SRC)
		return -EINVAL;

	dev_dbg(sd->dev, "%s: pad: %d sel(%d,%d)/%dx%d\n", __func__,
		sel->pad, sel->r.left, sel->r.top, sel->r.width, sel->r.height);

	rkisp1_rsz_set_in_crop(rsz, cfg, &sel->r, sel->which);

	return 0;
}

static const struct media_entity_operations rkisp1_rsz_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_pad_ops rkisp1_rsz_pad_ops = {
	.enum_mbus_code = rkisp1_rsz_enum_mbus_code,
	.get_selection = rkisp1_rsz_get_selection,
	.set_selection = rkisp1_rsz_set_selection,
	.init_cfg = rkisp1_rsz_init_config,
	.get_fmt = rkisp1_rsz_get_fmt,
	.set_fmt = rkisp1_rsz_set_fmt,
	.link_validate = v4l2_subdev_link_validate_default,
};

/* ----------------------------------------------------------------------------
 * Stream operations
 */

static int rkisp1_rsz_s_stream(struct v4l2_subdev *sd, int on)
{
	struct rkisp1_resizer *rsz = container_of(sd, struct rkisp1_resizer,
						  sd);
	struct rkisp1_device *rkisp1 = rsz->rkisp1;
	struct rkisp1_capture *other = &rkisp1->capture_devs[rsz->id ^ 1];
	enum rkisp1_shadow_regs_when when = RKISP1_SHADOW_REGS_SYNC;

	if (!on) {
		rkisp1_dcrop_disable(rsz, RKISP1_SHADOW_REGS_ASYNC);
		rkisp1_rsz_disable(rsz, RKISP1_SHADOW_REGS_ASYNC);
		return 0;
	}

	/* TODO: I don't think the driver supports streaming from both captures
	 * at the same time, maybe it should fail here */
	if (other->streaming)
		when = RKISP1_SHADOW_REGS_ASYNC;

	rkisp1_rsz_config(rsz, when);
	rkisp1_dcrop_config(rsz);

	return 0;
}

static const struct v4l2_subdev_video_ops rkisp1_rsz_video_ops = {
	.s_stream = rkisp1_rsz_s_stream,
};

static const struct v4l2_subdev_ops rkisp1_rsz_ops = {
	.video = &rkisp1_rsz_video_ops,
	.pad = &rkisp1_rsz_pad_ops,
};

static void rkisp1_rsz_unregister(struct rkisp1_resizer *rsz)
{
	v4l2_device_unregister_subdev(&rsz->sd);
	media_entity_cleanup(&rsz->sd.entity);
}

static int rkisp1_rsz_register(struct rkisp1_resizer *rsz)
{
	struct media_pad *pads = rsz->pads;
	struct v4l2_subdev *sd = &rsz->sd;
	int ret;

	if (rsz->id == RKISP1_SELFPATH)
		rsz->config = &rkisp1_rsz_config_sp;
	else
		rsz->config = &rkisp1_rsz_config_mp;

	v4l2_subdev_init(sd, &rkisp1_rsz_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->entity.ops = &rkisp1_rsz_media_ops;
	strscpy(sd->name, "rkisp1-resizer-subdev", sizeof(sd->name));

	pads[RKISP1_RSZ_PAD_SINK].flags = MEDIA_PAD_FL_SINK |
					  MEDIA_PAD_FL_MUST_CONNECT;
	pads[RKISP1_RSZ_PAD_SRC].flags = MEDIA_PAD_FL_SOURCE |
					 MEDIA_PAD_FL_MUST_CONNECT;

	rsz->fmt_type = RKISP1_DEF_FMT_TYPE;
	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_SCALER;
	ret = media_entity_pads_init(&sd->entity, 2, pads);
	if (ret)
		return ret;

	sd->owner = THIS_MODULE;
	v4l2_set_subdevdata(sd, rsz);

	ret = v4l2_device_register_subdev(&rsz->rkisp1->v4l2_dev, sd);
	if (ret) {
		dev_err(sd->dev, "Failed to register resizer subdev\n");
		goto err_cleanup_media_entity;
	}

	rkisp1_rsz_init_config(sd, rsz->pad_cfg);
	return 0;

err_cleanup_media_entity:
	media_entity_cleanup(&sd->entity);

	return ret;
}

int rkisp1_resizer_devs_register(struct rkisp1_device *rkisp1)
{
	struct rkisp1_resizer *rsz;
	unsigned int i, j;
	int ret;

	for (i = 0; i < ARRAY_SIZE(rkisp1->resizer_devs); i++) {
		rsz = &rkisp1->resizer_devs[i];
		rsz->rkisp1 = rkisp1;
		rsz->id = i;
		ret = rkisp1_rsz_register(rsz);
		if (ret)
			goto err_unreg_resizer_devs;
	}

	return 0;

err_unreg_resizer_devs:
	for (j = 0; j < i; j++) {
		rsz = &rkisp1->resizer_devs[j];
		rkisp1_rsz_unregister(rsz);
	}

	return ret;
}

void rkisp1_resizer_devs_unregister(struct rkisp1_device *rkisp1)
{
	struct rkisp1_resizer *mp = &rkisp1->resizer_devs[RKISP1_MAINPATH];
	struct rkisp1_resizer *sp = &rkisp1->resizer_devs[RKISP1_SELFPATH];

	rkisp1_rsz_unregister(mp);
	rkisp1_rsz_unregister(sp);
}
