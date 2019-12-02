// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip ISP1 Driver - Registers
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#include <media/v4l2-common.h>

#include "regs.h"

void rkisp1_disable_dcrop(struct rkisp1_stream *stream, bool async)
{
	struct rkisp1_device *dev = stream->ispdev;
	u32 dc_ctrl = rkisp1_read(dev, stream->config->dual_crop.ctrl);
	u32 mask = ~(stream->config->dual_crop.yuvmode_mask |
			stream->config->dual_crop.rawmode_mask);

	dc_ctrl &= mask;
	if (async)
		dc_ctrl |= RKISP1_CIF_DUAL_CROP_GEN_CFG_UPD;
	else
		dc_ctrl |= RKISP1_CIF_DUAL_CROP_CFG_UPD;
	rkisp1_write(dev, dc_ctrl, stream->config->dual_crop.ctrl);
}

void rkisp1_config_dcrop(struct rkisp1_stream *stream, struct v4l2_rect *rect,
			 bool async)
{
	struct rkisp1_device *dev = stream->ispdev;
	u32 dc_ctrl = rkisp1_read(dev, stream->config->dual_crop.ctrl);

	rkisp1_write(dev, rect->left, stream->config->dual_crop.h_offset);
	rkisp1_write(dev, rect->top, stream->config->dual_crop.v_offset);
	rkisp1_write(dev, rect->width, stream->config->dual_crop.h_size);
	rkisp1_write(dev, rect->height, stream->config->dual_crop.v_size);
	dc_ctrl |= stream->config->dual_crop.yuvmode_mask;
	if (async)
		dc_ctrl |= RKISP1_CIF_DUAL_CROP_GEN_CFG_UPD;
	else
		dc_ctrl |= RKISP1_CIF_DUAL_CROP_CFG_UPD;
	rkisp1_write(dev, dc_ctrl, stream->config->dual_crop.ctrl);
}

void rkisp1_dump_rsz_regs(struct rkisp1_stream *stream)
{
	struct rkisp1_device *dev = stream->ispdev;

	dev_dbg(dev->dev,
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
		rkisp1_read(dev, stream->config->rsz.ctrl),
		rkisp1_read(dev, stream->config->rsz.ctrl_shd),
		rkisp1_read(dev, stream->config->rsz.scale_hy),
		rkisp1_read(dev, stream->config->rsz.scale_hy_shd),
		rkisp1_read(dev, stream->config->rsz.scale_hcb),
		rkisp1_read(dev, stream->config->rsz.scale_hcb_shd),
		rkisp1_read(dev, stream->config->rsz.scale_hcr),
		rkisp1_read(dev, stream->config->rsz.scale_hcr_shd),
		rkisp1_read(dev, stream->config->rsz.scale_vy),
		rkisp1_read(dev, stream->config->rsz.scale_vy_shd),
		rkisp1_read(dev, stream->config->rsz.scale_vc),
		rkisp1_read(dev, stream->config->rsz.scale_vc_shd),
		rkisp1_read(dev, stream->config->rsz.phase_hy),
		rkisp1_read(dev, stream->config->rsz.phase_hy_shd),
		rkisp1_read(dev, stream->config->rsz.phase_hc),
		rkisp1_read(dev, stream->config->rsz.phase_hc_shd),
		rkisp1_read(dev, stream->config->rsz.phase_vy),
		rkisp1_read(dev, stream->config->rsz.phase_vy_shd),
		rkisp1_read(dev, stream->config->rsz.phase_vc),
		rkisp1_read(dev, stream->config->rsz.phase_vc_shd));
}

static void rkisp1_update_rsz_shadow(struct rkisp1_stream *stream, bool async)
{
	struct rkisp1_device *dev = stream->ispdev;
	u32 ctrl_cfg = rkisp1_read(dev, stream->config->rsz.ctrl);

	if (async)
		ctrl_cfg |= RKISP1_CIF_RSZ_CTRL_CFG_UPD_AUTO;
	else
		ctrl_cfg |= RKISP1_CIF_RSZ_CTRL_CFG_UPD;

	rkisp1_write(dev, ctrl_cfg, stream->config->rsz.ctrl);
}

static void rkisp1_set_scale(struct rkisp1_stream *stream,
			     struct v4l2_rect *in_y,
			     struct v4l2_rect *in_c,
			     struct v4l2_rect *out_y,
			     struct v4l2_rect *out_c)
{
	u32 scale_hy, scale_hc, scale_vy, scale_vc, rsz_ctrl = 0;
	struct rkisp1_device *dev = stream->ispdev;

	if (in_y->width < out_y->width) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_HY_ENABLE |
			    RKISP1_CIF_RSZ_CTRL_SCALE_HY_UP;
		scale_hy = ((in_y->width - 1) * RKISP1_CIF_RSZ_SCALER_FACTOR) /
			   (out_y->width - 1);
		rkisp1_write(dev, scale_hy, stream->config->rsz.scale_hy);
	} else if (in_y->width > out_y->width) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_HY_ENABLE;
		scale_hy = ((out_y->width - 1) * RKISP1_CIF_RSZ_SCALER_FACTOR) /
			   (in_y->width - 1) + 1;
		rkisp1_write(dev, scale_hy, stream->config->rsz.scale_hy);
	}
	if (in_c->width < out_c->width) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_HC_ENABLE |
			    RKISP1_CIF_RSZ_CTRL_SCALE_HC_UP;
		scale_hc = ((in_c->width - 1) * RKISP1_CIF_RSZ_SCALER_FACTOR) /
			   (out_c->width - 1);
		rkisp1_write(dev, scale_hc, stream->config->rsz.scale_hcb);
		rkisp1_write(dev, scale_hc, stream->config->rsz.scale_hcr);
	} else if (in_c->width > out_c->width) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_HC_ENABLE;
		scale_hc = ((out_c->width - 1) * RKISP1_CIF_RSZ_SCALER_FACTOR) /
			   (in_c->width - 1) + 1;
		rkisp1_write(dev, scale_hc, stream->config->rsz.scale_hcb);
		rkisp1_write(dev, scale_hc, stream->config->rsz.scale_hcr);
	}

	if (in_y->height < out_y->height) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_VY_ENABLE |
			    RKISP1_CIF_RSZ_CTRL_SCALE_VY_UP;
		scale_vy = ((in_y->height - 1) * RKISP1_CIF_RSZ_SCALER_FACTOR) /
			   (out_y->height - 1);
		rkisp1_write(dev, scale_vy, stream->config->rsz.scale_vy);
	} else if (in_y->height > out_y->height) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_VY_ENABLE;
		scale_vy = ((out_y->height - 1) *
			    RKISP1_CIF_RSZ_SCALER_FACTOR) /
			   (in_y->height - 1) + 1;
		rkisp1_write(dev, scale_vy, stream->config->rsz.scale_vy);
	}

	if (in_c->height < out_c->height) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_VC_ENABLE |
			    RKISP1_CIF_RSZ_CTRL_SCALE_VC_UP;
		scale_vc = ((in_c->height - 1) * RKISP1_CIF_RSZ_SCALER_FACTOR) /
				(out_c->height - 1);
		rkisp1_write(dev, scale_vc, stream->config->rsz.scale_vc);
	} else if (in_c->height > out_c->height) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_VC_ENABLE;
		scale_vc = ((out_c->height - 1) *
			    RKISP1_CIF_RSZ_SCALER_FACTOR) /
			   (in_c->height - 1) + 1;
		rkisp1_write(dev, scale_vc, stream->config->rsz.scale_vc);
	}

	rkisp1_write(dev, rsz_ctrl, stream->config->rsz.ctrl);
}

void rkisp1_config_rsz(struct rkisp1_stream *stream, struct v4l2_rect *in_y,
		struct v4l2_rect *in_c, struct v4l2_rect *out_y,
		struct v4l2_rect *out_c, bool async)
{
	struct rkisp1_device *dev = stream->ispdev;
	unsigned int i;

	/* No phase offset */
	rkisp1_write(dev, 0, stream->config->rsz.phase_hy);
	rkisp1_write(dev, 0, stream->config->rsz.phase_hc);
	rkisp1_write(dev, 0, stream->config->rsz.phase_vy);
	rkisp1_write(dev, 0, stream->config->rsz.phase_vc);

	/* Linear interpolation */
	for (i = 0; i < 64; i++) {
		rkisp1_write(dev, i, stream->config->rsz.scale_lut_addr);
		rkisp1_write(dev, i, stream->config->rsz.scale_lut);
	}

	rkisp1_set_scale(stream, in_y, in_c, out_y, out_c);

	rkisp1_update_rsz_shadow(stream, async);
}

void rkisp1_disable_rsz(struct rkisp1_stream *stream, bool async)
{
	rkisp1_write(stream->ispdev, 0, stream->config->rsz.ctrl);

	if (!async)
		rkisp1_update_rsz_shadow(stream, async);
}

void rkisp1_config_mi_ctrl(struct rkisp1_stream *stream)
{
	struct rkisp1_device *dev = stream->ispdev;
	u32 mi_ctrl = rkisp1_read(dev, RKISP1_CIF_MI_CTRL);

	/* TODO: do we need to re-read the register all the time? */
	mi_ctrl = rkisp1_read(dev, RKISP1_CIF_MI_CTRL) & ~GENMASK(17, 16);
	mi_ctrl |= RKISP1_CIF_MI_CTRL_BURST_LEN_LUM_64;
	rkisp1_write(dev, mi_ctrl, RKISP1_CIF_MI_CTRL);

	mi_ctrl = rkisp1_read(dev, RKISP1_CIF_MI_CTRL) & ~GENMASK(19, 18);
	mi_ctrl |= RKISP1_CIF_MI_CTRL_BURST_LEN_CHROM_64;
	rkisp1_write(dev, mi_ctrl, RKISP1_CIF_MI_CTRL);

	mi_ctrl = rkisp1_read(dev, RKISP1_CIF_MI_CTRL);
	mi_ctrl |= RKISP1_CIF_MI_CTRL_INIT_BASE_EN;
	rkisp1_write(dev, mi_ctrl, RKISP1_CIF_MI_CTRL);

	mi_ctrl = rkisp1_read(dev, RKISP1_CIF_MI_CTRL);
	mi_ctrl |= RKISP1_CIF_MI_CTRL_INIT_OFFSET_EN;
	rkisp1_write(dev, mi_ctrl, RKISP1_CIF_MI_CTRL);
}

bool rkisp1_mp_is_stream_stopped(struct rkisp1_stream *stream)
{
	u32 en = RKISP1_CIF_MI_CTRL_SHD_MP_IN_ENABLED |
		 RKISP1_CIF_MI_CTRL_SHD_RAW_OUT_ENABLED;

	return !(rkisp1_read(stream->ispdev, RKISP1_CIF_MI_CTRL_SHD) & en);
}

bool rkisp1_sp_is_stream_stopped(struct rkisp1_stream *stream)
{
	return !(rkisp1_read(stream->ispdev, RKISP1_CIF_MI_CTRL_SHD) &
		 RKISP1_CIF_MI_CTRL_SHD_SP_IN_ENABLED);
}
