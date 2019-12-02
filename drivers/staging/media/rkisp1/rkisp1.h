/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Rockchip ISP1 Driver - ISP Subdevice header
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#ifndef _RKISP1_H
#define _RKISP1_H

#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "common.h"

struct rkisp1_stream;

#define RKISP1_DIR_OUT BIT(0)
#define RKISP1_DIR_IN BIT(1)
#define RKISP1_DIR_IN_OUT (RKISP1_DIR_IN | RKISP1_DIR_OUT)

/*
 * struct rkisp1_fmt - ISP pad format
 *
 * Translate mbus_code to hardware format values
 *
 * @bus_width: used for parallel
 */
struct rkisp1_fmt {
	u32 mbus_code;
	u8 fmt_type;
	u32 mipi_dt;
	u32 yuv_seq;
	u8 bus_width;
	enum rkisp1_fmt_raw_pat_type bayer_pat;
	unsigned int direction;
};

enum rkisp1_isp_pad {
	RKISP1_ISP_PAD_SINK_VIDEO,
	RKISP1_ISP_PAD_SINK_PARAMS,
	RKISP1_ISP_PAD_SOURCE_VIDEO,
	RKISP1_ISP_PAD_SOURCE_STATS,
	RKISP1_ISP_PAD_MAX
};

/*
 * struct rkisp1_isp_subdev - ISP sub-device
 *
 * See Cropping regions of ISP in rkisp1.c for details
 * @in_frm: input size, don't have to be equal to sensor size
 * @in_fmt: input format
 * @in_crop: crop for sink pad
 * @out_fmt: output format
 * @out_crop: output size
 *
 * @dphy_errctrl_disabled : if dphy errctrl is disabled (avoid endless interrupt)
 * @frm_sync_seq: frame sequence, to sync frame_id between video devices.
 * @quantization: output quantization
 *
 * TODO: remember to document all the fields after refactoring
 */
struct rkisp1_isp_subdev {
	struct v4l2_subdev sd;
	struct media_pad pads[RKISP1_ISP_PAD_MAX];
	struct v4l2_subdev_pad_config pad_cfg[RKISP1_ISP_PAD_MAX];
	const struct rkisp1_fmt *in_fmt;
	const struct rkisp1_fmt *out_fmt;
	bool dphy_errctrl_disabled;
	atomic_t frm_sync_seq;

};

struct v4l2_mbus_framefmt *
rkisp1_isp_sd_get_pad_fmt(struct rkisp1_isp_subdev *isp_sd,
			  struct v4l2_subdev_pad_config *cfg,
			  unsigned int pad, u32 which);

struct v4l2_rect *rkisp1_isp_sd_get_pad_crop(struct rkisp1_isp_subdev *isp_sd,
					     struct v4l2_subdev_pad_config *cfg,
					     unsigned int pad, u32 which);

int rkisp1_register_isp_subdev(struct rkisp1_device *isp_dev,
			       struct v4l2_device *v4l2_dev);

void rkisp1_unregister_isp_subdev(struct rkisp1_device *isp_dev);

void rkisp1_mipi_isr_thread(struct rkisp1_device *dev);

void rkisp1_isp_isr_thread(struct rkisp1_device *dev);

static inline struct rkisp1_isp_subdev *sd_to_isp_sd(struct v4l2_subdev *sd)
{
	return container_of(sd, struct rkisp1_isp_subdev, sd);
}

#endif /* _RKISP1_H */
