// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip ISP1 Driver - Resizer Subdevice
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */


#define RKISP1_DEF_PAD_FMT MEDIA_BUS_FMT_YUYV8_2X8

enum rkisp1_rsz_pad {
	RKISP1_RSZ_PAD_SINK,
	RKISP1_RSZ_PAD_SOURCE,
	RKISP1_RSZ_PAD_MAX
};

struct rkisp1_rsz_subdev {
	enum rkisp1_stream id;
	struct v4l2_subdev sd;
	struct media_pad pads[RKISP1_RSZ_PAD_MAX];
	struct v4l2_subdev_pad_config pad_cfg[RKISP1_RSZ_PAD_MAX];
	struct rsz_config *config;
	struct rkisp1_device *ispdev;
};

/* Different config between selfpath and mainpath */
struct rsz_config {
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

static struct rsz_config rkisp1_mp_rsz_config = {
	/* constraints */
	.max_rsz_width = STREAM_MAX_MP_RSZ_OUTPUT_WIDTH,
	.max_rsz_height = STREAM_MAX_MP_RSZ_OUTPUT_HEIGHT,
	.min_rsz_width = STREAM_MIN_RSZ_OUTPUT_WIDTH,
	.min_rsz_height = STREAM_MIN_RSZ_OUTPUT_HEIGHT,
	/* registers */
	.rsz = {
		.ctrl = CIF_MRSZ_CTRL,
		.scale_hy = CIF_MRSZ_SCALE_HY,
		.scale_hcr = CIF_MRSZ_SCALE_HCR,
		.scale_hcb = CIF_MRSZ_SCALE_HCB,
		.scale_vy = CIF_MRSZ_SCALE_VY,
		.scale_vc = CIF_MRSZ_SCALE_VC,
		.scale_lut = CIF_MRSZ_SCALE_LUT,
		.scale_lut_addr = CIF_MRSZ_SCALE_LUT_ADDR,
		.scale_hy_shd = CIF_MRSZ_SCALE_HY_SHD,
		.scale_hcr_shd = CIF_MRSZ_SCALE_HCR_SHD,
		.scale_hcb_shd = CIF_MRSZ_SCALE_HCB_SHD,
		.scale_vy_shd = CIF_MRSZ_SCALE_VY_SHD,
		.scale_vc_shd = CIF_MRSZ_SCALE_VC_SHD,
		.phase_hy = CIF_MRSZ_PHASE_HY,
		.phase_hc = CIF_MRSZ_PHASE_HC,
		.phase_vy = CIF_MRSZ_PHASE_VY,
		.phase_vc = CIF_MRSZ_PHASE_VC,
		.ctrl_shd = CIF_MRSZ_CTRL_SHD,
		.phase_hy_shd = CIF_MRSZ_PHASE_HY_SHD,
		.phase_hc_shd = CIF_MRSZ_PHASE_HC_SHD,
		.phase_vy_shd = CIF_MRSZ_PHASE_VY_SHD,
		.phase_vc_shd = CIF_MRSZ_PHASE_VC_SHD,
	},
	.dual_crop = {
		.ctrl = CIF_DUAL_CROP_CTRL,
		.yuvmode_mask = CIF_DUAL_CROP_MP_MODE_YUV,
		.rawmode_mask = CIF_DUAL_CROP_MP_MODE_RAW,
		.h_offset = CIF_DUAL_CROP_M_H_OFFS,
		.v_offset = CIF_DUAL_CROP_M_V_OFFS,
		.h_size = CIF_DUAL_CROP_M_H_SIZE,
		.v_size = CIF_DUAL_CROP_M_V_SIZE,
	},
};

static struct rsz_config rkisp1_sp_rsz_config = {
	/* constraints */
	.max_rsz_width = STREAM_MAX_SP_RSZ_OUTPUT_WIDTH,
	.max_rsz_height = STREAM_MAX_SP_RSZ_OUTPUT_HEIGHT,
	.min_rsz_width = STREAM_MIN_RSZ_OUTPUT_WIDTH,
	.min_rsz_height = STREAM_MIN_RSZ_OUTPUT_HEIGHT,
	/* registers */
	.rsz = {
		.ctrl = CIF_SRSZ_CTRL,
		.scale_hy = CIF_SRSZ_SCALE_HY,
		.scale_hcr = CIF_SRSZ_SCALE_HCR,
		.scale_hcb = CIF_SRSZ_SCALE_HCB,
		.scale_vy = CIF_SRSZ_SCALE_VY,
		.scale_vc = CIF_SRSZ_SCALE_VC,
		.scale_lut = CIF_SRSZ_SCALE_LUT,
		.scale_lut_addr = CIF_SRSZ_SCALE_LUT_ADDR,
		.scale_hy_shd = CIF_SRSZ_SCALE_HY_SHD,
		.scale_hcr_shd = CIF_SRSZ_SCALE_HCR_SHD,
		.scale_hcb_shd = CIF_SRSZ_SCALE_HCB_SHD,
		.scale_vy_shd = CIF_SRSZ_SCALE_VY_SHD,
		.scale_vc_shd = CIF_SRSZ_SCALE_VC_SHD,
		.phase_hy = CIF_SRSZ_PHASE_HY,
		.phase_hc = CIF_SRSZ_PHASE_HC,
		.phase_vy = CIF_SRSZ_PHASE_VY,
		.phase_vc = CIF_SRSZ_PHASE_VC,
		.ctrl_shd = CIF_SRSZ_CTRL_SHD,
		.phase_hy_shd = CIF_SRSZ_PHASE_HY_SHD,
		.phase_hc_shd = CIF_SRSZ_PHASE_HC_SHD,
		.phase_vy_shd = CIF_SRSZ_PHASE_VY_SHD,
		.phase_vc_shd = CIF_SRSZ_PHASE_VC_SHD,
	},
	.dual_crop = {
		.ctrl = CIF_DUAL_CROP_CTRL,
		.yuvmode_mask = CIF_DUAL_CROP_SP_MODE_YUV,
		.rawmode_mask = CIF_DUAL_CROP_SP_MODE_RAW,
		.h_offset = CIF_DUAL_CROP_S_H_OFFS,
		.v_offset = CIF_DUAL_CROP_S_V_OFFS,
		.h_size = CIF_DUAL_CROP_S_H_SIZE,
		.v_size = CIF_DUAL_CROP_S_V_SIZE,
	},
};

static struct stream_config rkisp1_sp_stream_config = {
	.fmts = sp_fmts,
	.fmt_size = ARRAY_SIZE(sp_fmts),
	/* constraints */
	.max_rsz_width = STREAM_MAX_SP_RSZ_OUTPUT_WIDTH,
	.max_rsz_height = STREAM_MAX_SP_RSZ_OUTPUT_HEIGHT,
	.min_rsz_width = STREAM_MIN_RSZ_OUTPUT_WIDTH,
	.min_rsz_height = STREAM_MIN_RSZ_OUTPUT_HEIGHT,
	/* registers */
	.rsz = {
		.ctrl = CIF_SRSZ_CTRL,
		.scale_hy = CIF_SRSZ_SCALE_HY,
		.scale_hcr = CIF_SRSZ_SCALE_HCR,
		.scale_hcb = CIF_SRSZ_SCALE_HCB,
		.scale_vy = CIF_SRSZ_SCALE_VY,
		.scale_vc = CIF_SRSZ_SCALE_VC,
		.scale_lut = CIF_SRSZ_SCALE_LUT,
		.scale_lut_addr = CIF_SRSZ_SCALE_LUT_ADDR,
		.scale_hy_shd = CIF_SRSZ_SCALE_HY_SHD,
		.scale_hcr_shd = CIF_SRSZ_SCALE_HCR_SHD,
		.scale_hcb_shd = CIF_SRSZ_SCALE_HCB_SHD,
		.scale_vy_shd = CIF_SRSZ_SCALE_VY_SHD,
		.scale_vc_shd = CIF_SRSZ_SCALE_VC_SHD,
		.phase_hy = CIF_SRSZ_PHASE_HY,
		.phase_hc = CIF_SRSZ_PHASE_HC,
		.phase_vy = CIF_SRSZ_PHASE_VY,
		.phase_vc = CIF_SRSZ_PHASE_VC,
		.ctrl_shd = CIF_SRSZ_CTRL_SHD,
		.phase_hy_shd = CIF_SRSZ_PHASE_HY_SHD,
		.phase_hc_shd = CIF_SRSZ_PHASE_HC_SHD,
		.phase_vy_shd = CIF_SRSZ_PHASE_VY_SHD,
		.phase_vc_shd = CIF_SRSZ_PHASE_VC_SHD,
	},
	.dual_crop = {
		.ctrl = CIF_DUAL_CROP_CTRL,
		.yuvmode_mask = CIF_DUAL_CROP_SP_MODE_YUV,
		.rawmode_mask = CIF_DUAL_CROP_SP_MODE_RAW,
		.h_offset = CIF_DUAL_CROP_S_H_OFFS,
		.v_offset = CIF_DUAL_CROP_S_V_OFFS,
		.h_size = CIF_DUAL_CROP_S_H_SIZE,
		.v_size = CIF_DUAL_CROP_S_V_SIZE,
	},
	.mi = {
		.y_size_init = CIF_MI_SP_Y_SIZE_INIT,
		.cb_size_init = CIF_MI_SP_CB_SIZE_INIT,
		.cr_size_init = CIF_MI_SP_CR_SIZE_INIT,
		.y_base_ad_init = CIF_MI_SP_Y_BASE_AD_INIT,
		.cb_base_ad_init = CIF_MI_SP_CB_BASE_AD_INIT,
		.cr_base_ad_init = CIF_MI_SP_CR_BASE_AD_INIT,
		.y_offs_cnt_init = CIF_MI_SP_Y_OFFS_CNT_INIT,
		.cb_offs_cnt_init = CIF_MI_SP_CB_OFFS_CNT_INIT,
		.cr_offs_cnt_init = CIF_MI_SP_CR_OFFS_CNT_INIT,
	},
};


static inline struct rkisp1_rsz_subdev *sd_to_rsz_sd(struct v4l2_subdev *sd)
{
	return container_of(sd, struct rkisp1_rsz_subdev, sd);
}


/* configure dual-crop unit */
static int rkisp1_config_dcrop(struct rkisp1_stream *stream, bool async)
{
	struct rkisp1_device *dev = stream->ispdev;
	const struct v4l2_rect *fmt;
	struct v4l2_rect *dcrop;

	dcrop = rkisp1_rsz_sd_get_pad_crop(rsz_sd, rsz_sd->pad_cfg,
					   RKISP1_RSZ_PAD_SINK,
					   V4L2_SUBDEV_FORMAT_ACTIVE);
	fmt = rkisp1_rsz_sd_get_pad_fmt(rsz_sd, rsz_sd->pad_cfg,
					RKISP1_RSZ_PAD_SINK,
					V4L2_SUBDEV_FORMAT_ACTIVE);

	if (dcrop->width == fmt->width &&
	    dcrop->height == fmt->height &&
	    dcrop->left == 0 && dcrop->top == 0) {
		disable_dcrop(stream, async);
		dev_dbg(dev->dev, "stream %d crop disabled\n", stream->id);
		return 0;
	}

	config_dcrop(stream, dcrop, async);

	dev_dbg(dev->dev, "stream %d crop: %dx%d -> %dx%d\n", stream->id,
		fmt->width, fmt->height, dcrop->width, dcrop->height);

	return 0;
}

// ---------------------------- subdev api --------------------------
struct v4l2_mbus_framefmt *
rkisp1_rsz_sd_get_pad_fmt(struct rkisp1_rsz_subdev *rsz_sd,
			  struct v4l2_subdev_pad_config *cfg,
			  unsigned int pad, u32 which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&rsz_sd->sd, cfg, pad);
	else
		return v4l2_subdev_get_try_format(&rsz_sd->sd,
						  rsz_sd->pad_cfg, pad);
}

static int rkisp1_rsz_sd_get_fmt(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_format *fmt)
{
	struct rkisp1_rsz_subdev *rsz_sd = sd_to_rsz_sd(sd);

	fmt->format = *rkisp1_rsz_sd_get_pad_fmt(rsz_sd, cfg, fmt->pad,
						 fmt->which);
	return 0;
}

static int rkisp1_rsz_sd_set_fmt(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_format *fmt)
{
	struct rkisp1_rsz_subdev *rsz_sd = sd_to_rsz_sd(sd);
	struct v4l2_mbus_framefmt *mfmt;

	// TODO
	mfmt = rkisp1_rsz_sd_get_pad_fmt(rsz_sd, cfg, fmt->pad, which);
	*format = *out_fmt;

	return 0;
}

static int rkisp1_rsz_sd_init_config(struct v4l2_subdev *sd,
				     struct v4l2_subdev_pad_config *cfg)
{
	struct v4l2_rect *mf_in_crop, *mf_out_crop;
	struct v4l2_mbus_framefmt *mf_in, *mf_out;

	mf_in = v4l2_subdev_get_try_format(sd, cfg, RKISP1_RSZ_PAD_SINK);
	mf_in->width = RKISP1_DEFAULT_WIDTH;
	mf_in->height = RKISP1_DEFAULT_HEIGHT;
	mf_in->field = V4L2_FIELD_NONE;
	mf_in->code = RKISP1_DEF_PAD_FMT;
	mf_in->quantization = V4L2_QUANTIZATION_FULL_RANGE;

	mf_in_crop = v4l2_subdev_get_try_crop(sd, cfg, RKISP1_RSZ_PAD_SINK);
	mf_in_crop->width = RKISP1_DEFAULT_WIDTH;
	mf_in_crop->height = RKISP1_DEFAULT_HEIGHT;
	mf_in_crop->left = 0;
	mf_in_crop->top = 0;

	mf_out = v4l2_subdev_get_try_format(sd, cfg, RKISP1_RSZ_PAD_SOURCE);
	*mf_out = *mf_in;

	mf_out_crop = v4l2_subdev_get_try_crop(sd, cfg, RKISP1_RSZ_PAD_SOURCE);
	*mf_out_crop = *mf_in_crop;

	return 0;
}

static int rkisp1_rsz_sd_get_selection(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_selection *sel)
{
	struct rkisp1_rsz_subdev *rsz_sd = sd_to_rsz_sd(sd);

	if (sel->pad != RKISP1_ISP_PAD_SOURCE_VIDEO &&
	    sel->pad != RKISP1_ISP_PAD_SINK_VIDEO)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		if (sel->pad == RKISP1_ISP_PAD_SINK_VIDEO) {
			struct v4l2_mbus_framefmt *__format;

			__format = rkisp1_rsz_sd_get_pad_fmt(rsz_sd, cfg,
							     sel->pad,
							     sel->which);
			sel->r.height = __format->height;
			sel->r.width = __format->width;
			sel->r.left = 0;
			sel->r.top = 0;
		} else {
			sel->r = *rkisp1_rsz_sd_get_pad_crop(rsz_sd, cfg,
						RKISP1_ISP_PAD_SINK_VIDEO,
						sel->which);
		}
		break;
	case V4L2_SEL_TGT_CROP:
		sel->r = *rkisp1_rsz_sd_get_pad_crop(rsz_sd, cfg, sel->pad,
						     sel->which);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rkisp1_rsz_sd_set_selection(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_selection *sel)
{
	struct rkisp1_rsz_subdev *rsz_sd = sd_to_rsz_sd(sd);
	struct rkisp1_device *dev = sd_to_rsz_dev(sd);
	const struct v4l2_rect *crop;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	dev_dbg(dev->dev, "%s: pad: %d sel(%d,%d)/%dx%d\n", __func__, sel->pad,
		sel->r.left, sel->r.top, sel->r.width, sel->r.height);

	// TODO
	crop = rkisp1_rsz_sd_get_pad_crop(rsz_sd, cfg, sel->pad, which);
	*crop = *r;

	return 0;
}

static int rkisp1_rsz_sd_enum_mbus_code(struct v4l2_subdev *sd,
					struct v4l2_subdev_pad_config *cfg,
					struct v4l2_subdev_mbus_code_enum *code)
{
	// TODO
	return -EINVAL;
}

static int mbus_code_xysubs(u32 code, u32 *xsubs, u32 *ysubs)
{
	switch (code) {
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YVYU8_1X16:
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_VYUY8_1X16:
		*xsubs = 2;
		*ysubs = 1;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

// TODO: replace this by v4l2_format_info()
/* Get xsubs and ysubs for fourcc formats
 *
 * @xsubs: horizontal color samples in a 4*4 matrix, for yuv
 * @ysubs: vertical color samples in a 4*4 matrix, for yuv
 */
static int fcc_xysubs(u32 fcc, u32 *xsubs, u32 *ysubs)
{
	switch (fcc) {
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_YUV444M:
		*xsubs = 1;
		*ysubs = 1;
		break;
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_YVU422M:
		*xsubs = 2;
		*ysubs = 1;
		break;
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21M:
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		*xsubs = 2;
		*ysubs = 2;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/* configure scale unit */
// TODO
static int rkisp1_config_rsz(struct rkisp1_rsz_subdev *rsz_sd, bool async)
{
	struct rkisp1_device *dev = rsz_dev->ispdev;
	struct capture_fmt *output_isp_fmt = &stream->out_isp_fmt;
	const struct rkisp1_fmt *input_isp_fmt = dev->isp_sdev.out_fmt;
	struct v4l2_rect *in_y, *out_y;
	struct v4l2_rect in_c, out_c;
	u32 xsubs_in, ysubs_in, xsubs_out, ysubs_out;

	if (input_isp_fmt->fmt_type == FMT_BAYER)
		goto disable;

	/* set input and output sizes for scale calculation */
	in_y = rkisp1_rsz_sd_get_pad_crop(rsz_sd, rsz_sd->pad_cfg,
					   RKISP1_RSZ_PAD_SOURCE,
					   V4L2_SUBDEV_FORMAT_ACTIVE);
	out_y = rkisp1_rsz_sd_get_pad_crop(rsz_sd, rsz_sd->pad_cfg,
					   RKISP1_RSZ_PAD_SOURCE,
					   V4L2_SUBDEV_FORMAT_ACTIVE);

	/* The size of Cb,Cr are related to the format */
	if (mbus_code_xysubs(input_isp_fmt->mbus_code, &xsubs_in, &ysubs_in)) {
		dev_err(dev->dev, "Not xsubs/ysubs found\n");
		return -EINVAL;
	}
	in_c.width = in_y.width / xsubs_in;
	in_c.height = in_y.height / ysubs_in;

	if (output_isp_fmt->fmt_type == FMT_YUV) {
		fcc_xysubs(output_isp_fmt->fourcc, &xsubs_out, &ysubs_out);
		out_c.width = out_y.width / xsubs_out;
		out_c.height = out_y.height / ysubs_out;
	} else {
		out_c.width = out_y.width / xsubs_in;
		out_c.height = out_y.height / ysubs_in;
	}

	if (in_c.width == out_c.width && in_c.height == out_c.height)
		goto disable;

	/* set RSZ input and output */
	dev_dbg(dev->dev, "stream %d rsz/scale: %dx%d -> %dx%d\n",
		stream->id, stream->dcrop.width, stream->dcrop.height,
		output_fmt.width, output_fmt.height);
	dev_dbg(dev->dev, "chroma scaling %dx%d -> %dx%d\n",
		in_c.width, in_c.height, out_c.width, out_c.height);

	/* calculate and set scale */
	config_rsz(stream, &in_y, &in_c, &out_y, &out_c, async);

	dump_rsz_regs(dev->dev, stream);

	return 0;

disable:
	disable_rsz(stream, async);

	return 0;
}

static int rkisp1_rsz_sd_s_stream(struct v4l2_subdev *sd, int on)
{
	struct rkisp1_device *dev = rsz_sd->ispdev;
	struct rkisp1_rsz_subdev *rsz_sd = sd_to_rsz_sd(sd);
	int ret;

	if (on) {
		struct rkisp1_stream *other = &dev->stream[stream->id ^ 1];
		bool async = false;

		if (other->streaming)
			async = true;

		ret = rkisp1_config_rsz(stream, async);
		if (ret < 0) {
			dev_err(dev->dev, "config rsz failed with error %d\n", ret);
			return ret;
		}

		/*
		 * can't be async now, otherwise the latter started stream fails to
		 * produce mi interrupt.
		 */
		ret = rkisp1_config_dcrop(stream, false);
		if (ret < 0) {
			dev_err(dev->dev, "config dcrop failed with error %d\n", ret);
			return ret;
		}
	} else {
		disable_dcrop(rsz_sd, true);
		disable_rsz(rsz_sd, true);
	}

	return 0;
}

static const struct v4l2_subdev_pad_ops rkisp1_rsz_sd_pad_ops = {
	.enum_mbus_code = rkisp1_rsz_sd_enum_mbus_code,
	.get_selection = rkisp1_rsz_sd_get_selection,
	.set_selection = rkisp1_rsz_sd_set_selection,
	.init_cfg = rkisp1_rsz_sd_init_config,
	.get_fmt = rkisp1_rsz_sd_get_fmt,
	.set_fmt = rkisp1_rsz_sd_set_fmt,
	.link_validate = v4l2_subdev_link_validate_default,
};

static const struct media_entity_operations rkisp1_rsz_sd_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_video_ops rkisp1_rsz_sd_video_ops = {
	.s_stream = rkisp1_rsz_sd_s_stream,
};

static const struct v4l2_subdev_ops rkisp1_rsz_sd_ops = {
	.video = &rkisp1_rsz_sd_video_ops,
	.pad = &rkisp1_rsz_sd_pad_ops,
};

void rkisp1_register_rsz_subdev(struct rkisp1_device *dev,
				enum rkisp1_stream id)
{
	struct rkisp1_rsz rsz_sd = &dev->rsz_sdev[id];
	struct media_pad *pads = &rsz_sd->pads;
	struct v4l2_subdev *sd = &rsz_sd->sd;
	int ret;

	// TODO check if this memset is required
	memset(rsz_sd, 0, sizeof(*rsz_sd));
	rsz_sd->id = id;
	rsz_sd->ispdev = dev;

	v4l2_subdev_init(sd, &rkisp1_rsz_sd_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->entity.ops = &rkisp1_rsz_sd_media_ops;
	strscpy(sd->name, "rkisp1-rsz-subdev", sizeof(sd->name));

	pads[RKISP1_RSZ_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[RKISP1_RSZ_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_SCALER;
	ret = media_entity_pads_init(&sd->entity, RKISP1_RSZ_PAD_MAX, pads);
	if (ret < 0)
		return ret;

	sd->owner = THIS_MODULE;
	v4l2_set_subdevdata(sd, isp_dev);

	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret < 0) {
		dev_err(sd->dev, "Failed to register isp subdev\n");
		goto err_cleanup_media_entity;
	}

	rkisp1_rsz_sd_init_config(sd, rsz_dev->pad_cfg);

	if (stream->id == RKISP1_STREAM_SP) {
		stream->ops = &rkisp1_sp_streams_ops;
		stream->config = &rkisp1_sp_stream_config;
	} else {
		stream->ops = &rkisp1_mp_streams_ops;
		stream->config = &rkisp1_mp_stream_config;
	}

	return 0;

err_cleanup_media_entity:
	media_entity_cleanup(&sd->entity);

	return ret;
}

void rkisp1_unregister_rsz_subdev(struct rkisp1_rsz *rsz_sd)
{
	v4l2_device_unregister_subdev(&rsz_sd->sd);
	media_entity_cleanup(&sd->entity);
}
