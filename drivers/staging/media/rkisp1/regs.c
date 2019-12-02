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
	void __iomem *base = stream->ispdev->base_addr;
	void __iomem *dc_ctrl_addr = base + stream->config->dual_crop.ctrl;
	u32 dc_ctrl = readl(dc_ctrl_addr);
	u32 mask = ~(stream->config->dual_crop.yuvmode_mask |
			stream->config->dual_crop.rawmode_mask);
	u32 val = dc_ctrl & mask;

	if (async)
		val |= RKISP1_CIF_DUAL_CROP_GEN_CFG_UPD;
	else
		val |= RKISP1_CIF_DUAL_CROP_CFG_UPD;
	writel(val, dc_ctrl_addr);
}

void rkisp1_config_dcrop(struct rkisp1_stream *stream, struct v4l2_rect *rect,
			 bool async)
{
	void __iomem *base = stream->ispdev->base_addr;
	void __iomem *dc_ctrl_addr = base + stream->config->dual_crop.ctrl;
	u32 dc_ctrl = readl(dc_ctrl_addr);

	writel(rect->left, base + stream->config->dual_crop.h_offset);
	writel(rect->top, base + stream->config->dual_crop.v_offset);
	writel(rect->width, base + stream->config->dual_crop.h_size);
	writel(rect->height, base + stream->config->dual_crop.v_size);
	dc_ctrl |= stream->config->dual_crop.yuvmode_mask;
	if (async)
		dc_ctrl |= RKISP1_CIF_DUAL_CROP_GEN_CFG_UPD;
	else
		dc_ctrl |= RKISP1_CIF_DUAL_CROP_CFG_UPD;
	writel(dc_ctrl, dc_ctrl_addr);
}

void rkisp1_dump_rsz_regs(struct device *dev, struct rkisp1_stream *stream)
{
	void __iomem *base = stream->ispdev->base_addr;

	dev_dbg(dev,
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
		readl(base + stream->config->rsz.ctrl),
		readl(base + stream->config->rsz.ctrl_shd),
		readl(base + stream->config->rsz.scale_hy),
		readl(base + stream->config->rsz.scale_hy_shd),
		readl(base + stream->config->rsz.scale_hcb),
		readl(base + stream->config->rsz.scale_hcb_shd),
		readl(base + stream->config->rsz.scale_hcr),
		readl(base + stream->config->rsz.scale_hcr_shd),
		readl(base + stream->config->rsz.scale_vy),
		readl(base + stream->config->rsz.scale_vy_shd),
		readl(base + stream->config->rsz.scale_vc),
		readl(base + stream->config->rsz.scale_vc_shd),
		readl(base + stream->config->rsz.phase_hy),
		readl(base + stream->config->rsz.phase_hy_shd),
		readl(base + stream->config->rsz.phase_hc),
		readl(base + stream->config->rsz.phase_hc_shd),
		readl(base + stream->config->rsz.phase_vy),
		readl(base + stream->config->rsz.phase_vy_shd),
		readl(base + stream->config->rsz.phase_vc),
		readl(base + stream->config->rsz.phase_vc_shd));
}

static void rkisp1_update_rsz_shadow(struct rkisp1_stream *stream, bool async)
{
	void __iomem *addr =
		stream->ispdev->base_addr + stream->config->rsz.ctrl;
	u32 ctrl_cfg = readl(addr);

	if (async)
		writel(RKISP1_CIF_RSZ_CTRL_CFG_UPD_AUTO | ctrl_cfg, addr);
	else
		writel(RKISP1_CIF_RSZ_CTRL_CFG_UPD | ctrl_cfg, addr);
}

static void rkisp1_set_scale(struct rkisp1_stream *stream,
			     struct v4l2_rect *in_y,
			     struct v4l2_rect *in_c,
			     struct v4l2_rect *out_y,
			     struct v4l2_rect *out_c)
{
	void __iomem *base = stream->ispdev->base_addr;
	void __iomem *scale_hy_addr = base + stream->config->rsz.scale_hy;
	void __iomem *scale_hcr_addr = base + stream->config->rsz.scale_hcr;
	void __iomem *scale_hcb_addr = base + stream->config->rsz.scale_hcb;
	void __iomem *scale_vy_addr = base + stream->config->rsz.scale_vy;
	void __iomem *scale_vc_addr = base + stream->config->rsz.scale_vc;
	void __iomem *rsz_ctrl_addr = base + stream->config->rsz.ctrl;
	u32 scale_hy, scale_hc, scale_vy, scale_vc, rsz_ctrl = 0;

	if (in_y->width < out_y->width) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_HY_ENABLE |
			    RKISP1_CIF_RSZ_CTRL_SCALE_HY_UP;
		scale_hy = ((in_y->width - 1) * RKISP1_CIF_RSZ_SCALER_FACTOR) /
			   (out_y->width - 1);
		writel(scale_hy, scale_hy_addr);
	} else if (in_y->width > out_y->width) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_HY_ENABLE;
		scale_hy = ((out_y->width - 1) * RKISP1_CIF_RSZ_SCALER_FACTOR) /
			   (in_y->width - 1) + 1;
		writel(scale_hy, scale_hy_addr);
	}
	if (in_c->width < out_c->width) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_HC_ENABLE |
			    RKISP1_CIF_RSZ_CTRL_SCALE_HC_UP;
		scale_hc = ((in_c->width - 1) * RKISP1_CIF_RSZ_SCALER_FACTOR) /
			   (out_c->width - 1);
		writel(scale_hc, scale_hcb_addr);
		writel(scale_hc, scale_hcr_addr);
	} else if (in_c->width > out_c->width) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_HC_ENABLE;
		scale_hc = ((out_c->width - 1) * RKISP1_CIF_RSZ_SCALER_FACTOR) /
			   (in_c->width - 1) + 1;
		writel(scale_hc, scale_hcb_addr);
		writel(scale_hc, scale_hcr_addr);
	}

	if (in_y->height < out_y->height) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_VY_ENABLE |
			    RKISP1_CIF_RSZ_CTRL_SCALE_VY_UP;
		scale_vy = ((in_y->height - 1) * RKISP1_CIF_RSZ_SCALER_FACTOR) /
			   (out_y->height - 1);
		writel(scale_vy, scale_vy_addr);
	} else if (in_y->height > out_y->height) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_VY_ENABLE;
		scale_vy = ((out_y->height - 1) *
			    RKISP1_CIF_RSZ_SCALER_FACTOR) /
			   (in_y->height - 1) + 1;
		writel(scale_vy, scale_vy_addr);
	}

	if (in_c->height < out_c->height) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_VC_ENABLE |
			    RKISP1_CIF_RSZ_CTRL_SCALE_VC_UP;
		scale_vc = ((in_c->height - 1) * RKISP1_CIF_RSZ_SCALER_FACTOR) /
				(out_c->height - 1);
		writel(scale_vc, scale_vc_addr);
	} else if (in_c->height > out_c->height) {
		rsz_ctrl |= RKISP1_CIF_RSZ_CTRL_SCALE_VC_ENABLE;
		scale_vc = ((out_c->height - 1) *
			    RKISP1_CIF_RSZ_SCALER_FACTOR) /
			   (in_c->height - 1) + 1;
		writel(scale_vc, scale_vc_addr);
	}

	writel(rsz_ctrl, rsz_ctrl_addr);
}

void rkisp1_config_rsz(struct rkisp1_stream *stream, struct v4l2_rect *in_y,
		struct v4l2_rect *in_c, struct v4l2_rect *out_y,
		struct v4l2_rect *out_c, bool async)
{
	void __iomem *base_addr = stream->ispdev->base_addr;
	unsigned int i;

	/* No phase offset */
	writel(0, base_addr + stream->config->rsz.phase_hy);
	writel(0, base_addr + stream->config->rsz.phase_hc);
	writel(0, base_addr + stream->config->rsz.phase_vy);
	writel(0, base_addr + stream->config->rsz.phase_vc);

	/* Linear interpolation */
	for (i = 0; i < 64; i++) {
		writel(i, base_addr + stream->config->rsz.scale_lut_addr);
		writel(i, base_addr + stream->config->rsz.scale_lut);
	}

	rkisp1_set_scale(stream, in_y, in_c, out_y, out_c);

	rkisp1_update_rsz_shadow(stream, async);
}

void rkisp1_disable_rsz(struct rkisp1_stream *stream, bool async)
{
	writel(0, stream->ispdev->base_addr + stream->config->rsz.ctrl);

	if (!async)
		rkisp1_update_rsz_shadow(stream, async);
}

void rkisp1_config_mi_ctrl(struct rkisp1_stream *stream)
{
	void __iomem *base = stream->ispdev->base_addr;
	void __iomem *addr = base + RKISP1_CIF_MI_CTRL;
	u32 reg;

	reg = readl(addr) & ~GENMASK(17, 16);
	writel(reg | RKISP1_CIF_MI_CTRL_BURST_LEN_LUM_64, addr);
	reg = readl(addr) & ~GENMASK(19, 18);
	writel(reg | RKISP1_CIF_MI_CTRL_BURST_LEN_CHROM_64, addr);
	reg = readl(addr);
	writel(reg | RKISP1_CIF_MI_CTRL_INIT_BASE_EN, addr);
	reg = readl(addr);
	writel(reg | RKISP1_CIF_MI_CTRL_INIT_OFFSET_EN, addr);
}

bool rkisp1_mp_is_stream_stopped(void __iomem *base)
{
	u32 en = RKISP1_CIF_MI_CTRL_SHD_MP_IN_ENABLED |
		 RKISP1_CIF_MI_CTRL_SHD_RAW_OUT_ENABLED;

	return !(readl(base + RKISP1_CIF_MI_CTRL_SHD) & en);
}

bool rkisp1_sp_is_stream_stopped(void __iomem *base)
{
	return !(readl(base + RKISP1_CIF_MI_CTRL_SHD) &
		 RKISP1_CIF_MI_CTRL_SHD_SP_IN_ENABLED);
}
