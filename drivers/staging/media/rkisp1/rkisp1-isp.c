// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip ISP1 Driver - ISP Subdevice
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#include <linux/iopoll.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-mipi-dphy.h>
#include <linux/pm_runtime.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>
#include <media/v4l2-event.h>

#include "rkisp1-common.h"

#define RKISP1_CIF_ISP_INPUT_W_MAX		4032
#define RKISP1_CIF_ISP_INPUT_H_MAX		3024
#define RKISP1_CIF_ISP_INPUT_W_MIN		32
#define RKISP1_CIF_ISP_INPUT_H_MIN		32
#define RKISP1_CIF_ISP_OUTPUT_W_MAX		RKISP1_CIF_ISP_INPUT_W_MAX
#define RKISP1_CIF_ISP_OUTPUT_H_MAX		RKISP1_CIF_ISP_INPUT_H_MAX
#define RKISP1_CIF_ISP_OUTPUT_W_MIN		RKISP1_CIF_ISP_INPUT_W_MIN
#define RKISP1_CIF_ISP_OUTPUT_H_MIN		RKISP1_CIF_ISP_INPUT_H_MIN

#define RKISP1_DEF_SINK_PAD_FMT MEDIA_BUS_FMT_SRGGB10_1X10
#define RKISP1_DEF_SRC_PAD_FMT MEDIA_BUS_FMT_YUYV8_2X8

/*
 * NOTE: MIPI controller and input MUX are also configured in this file,
 * because ISP Subdev is not only describe ISP submodule(input size,format,
 * output size, format), but also a virtual route device.
 */

/*
 * There are many variables named with format/frame in below code,
 * please see here for their meaning.
 *
 * Cropping regions of ISP
 *
 * +---------------------------------------------------------+
 * | Sensor image                                            |
 * | +---------------------------------------------------+   |
 * | | ISP_ACQ (for black level)                         |   |
 * | | in_frm                                            |   |
 * | | +--------------------------------------------+    |   |
 * | | |    ISP_OUT                                 |    |   |
 * | | |    in_crop                                 |    |   |
 * | | |    +---------------------------------+     |    |   |
 * | | |    |   ISP_IS                        |     |    |   |
 * | | |    |   rkisp1_isp_subdev: out_crop   |     |    |   |
 * | | |    +---------------------------------+     |    |   |
 * | | +--------------------------------------------+    |   |
 * | +---------------------------------------------------+   |
 * +---------------------------------------------------------+
 */

static inline struct rkisp1_isp_subdev *
rkisp1_sd_to_isp_sd(struct v4l2_subdev *sd)
{
	return container_of(sd, struct rkisp1_isp_subdev, sd);
}

/* Get sensor by enabled media link */
static struct v4l2_subdev *rkisp1_get_remote_sensor(struct v4l2_subdev *sd)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;

	local = &sd->entity.pads[RKISP1_ISP_PAD_SINK_VIDEO];
	remote = media_entity_remote_pad(local);
	if (!remote) {
		dev_warn(sd->dev, "No link between isp and sensor\n");
		return NULL;
	}

	sensor_me = remote->entity;
	return media_entity_to_v4l2_subdev(sensor_me);
}

/****************  register operations ****************/

struct v4l2_mbus_framefmt *
rkisp1_isp_sd_get_pad_fmt(struct rkisp1_isp_subdev *isp_sd,
			  struct v4l2_subdev_pad_config *cfg,
			  unsigned int pad, u32 which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&isp_sd->sd, cfg, pad);
	else
		return v4l2_subdev_get_try_format(&isp_sd->sd,
						  isp_sd->pad_cfg, pad);
}

struct v4l2_rect *rkisp1_isp_sd_get_pad_crop(struct rkisp1_isp_subdev *isp_sd,
					     struct v4l2_subdev_pad_config *cfg,
					     unsigned int pad, u32 which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_crop(&isp_sd->sd, cfg, pad);
	else
		return v4l2_subdev_get_try_crop(&isp_sd->sd,
						isp_sd->pad_cfg, pad);
}

/*
 * Image Stabilization.
 * This should only be called when configuring CIF
 * or at the frame end interrupt
 */
static void rkisp1_config_ism(struct rkisp1_device *rkisp1)
{
	struct v4l2_rect *out_crop =
		rkisp1_isp_sd_get_pad_crop(&rkisp1->isp_sdev, NULL,
					   RKISP1_ISP_PAD_SOURCE_VIDEO,
					   V4L2_SUBDEV_FORMAT_ACTIVE);
	u32 val;

	rkisp1_write(rkisp1, 0, RKISP1_CIF_ISP_IS_RECENTER);
	rkisp1_write(rkisp1, 0, RKISP1_CIF_ISP_IS_MAX_DX);
	rkisp1_write(rkisp1, 0, RKISP1_CIF_ISP_IS_MAX_DY);
	rkisp1_write(rkisp1, 0, RKISP1_CIF_ISP_IS_DISPLACE);
	rkisp1_write(rkisp1, out_crop->left, RKISP1_CIF_ISP_IS_H_OFFS);
	rkisp1_write(rkisp1, out_crop->top, RKISP1_CIF_ISP_IS_V_OFFS);
	rkisp1_write(rkisp1, out_crop->width, RKISP1_CIF_ISP_IS_H_SIZE);
	rkisp1_write(rkisp1, out_crop->height, RKISP1_CIF_ISP_IS_V_SIZE);

	/* IS(Image Stabilization) is always on, working as output crop */
	rkisp1_write(rkisp1, 1, RKISP1_CIF_ISP_IS_CTRL);
	val = rkisp1_read(rkisp1, RKISP1_CIF_ISP_CTRL);
	val |= RKISP1_CIF_ISP_CTRL_ISP_CFG_UPD;
	rkisp1_write(rkisp1, val, RKISP1_CIF_ISP_CTRL);
}

/*
 * configure ISP blocks with input format, size......
 */
static int rkisp1_config_isp(struct rkisp1_device *rkisp1)
{
	u32 isp_ctrl = 0, irq_mask = 0, acq_mult = 0, signal = 0;
	const struct rkisp1_fmt *out_fmt, *in_fmt;
	struct v4l2_rect *in_crop;
	struct rkisp1_sensor_async *sensor;
	struct v4l2_mbus_framefmt *in_frm;

	sensor = rkisp1->active_sensor;
	in_fmt = rkisp1->isp_sdev.in_fmt;
	out_fmt = rkisp1->isp_sdev.out_fmt;
	in_frm = rkisp1_isp_sd_get_pad_fmt(&rkisp1->isp_sdev, NULL,
					   RKISP1_ISP_PAD_SINK_VIDEO,
					   V4L2_SUBDEV_FORMAT_ACTIVE);
	in_crop = rkisp1_isp_sd_get_pad_crop(&rkisp1->isp_sdev, NULL,
					     RKISP1_ISP_PAD_SINK_VIDEO,
					     V4L2_SUBDEV_FORMAT_ACTIVE);

	if (in_fmt->fmt_type == RKISP1_FMT_BAYER) {
		acq_mult = 1;
		if (out_fmt->fmt_type == RKISP1_FMT_BAYER) {
			if (sensor->mbus.type == V4L2_MBUS_BT656)
				isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_RAW_PICT_ITU656;
			else
				isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_RAW_PICT;
		} else {
			rkisp1_write(rkisp1, RKISP1_CIF_ISP_DEMOSAIC_TH(0xc),
				     RKISP1_CIF_ISP_DEMOSAIC);

			if (sensor->mbus.type == V4L2_MBUS_BT656)
				isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_BAYER_ITU656;
			else
				isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_BAYER_ITU601;
		}
	} else if (in_fmt->fmt_type == RKISP1_FMT_YUV) {
		acq_mult = 2;
		if (sensor->mbus.type == V4L2_MBUS_CSI2_DPHY) {
			isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_ITU601;
		} else {
			if (sensor->mbus.type == V4L2_MBUS_BT656)
				isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_ITU656;
			else
				isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_ITU601;
		}

		irq_mask |= RKISP1_CIF_ISP_DATA_LOSS;
	}

	/* Set up input acquisition properties */
	if (sensor->mbus.type == V4L2_MBUS_BT656 ||
	    sensor->mbus.type == V4L2_MBUS_PARALLEL) {
		if (sensor->mbus.flags & V4L2_MBUS_PCLK_SAMPLE_RISING)
			signal = RKISP1_CIF_ISP_ACQ_PROP_POS_EDGE;
	}

	if (sensor->mbus.type == V4L2_MBUS_PARALLEL) {
		if (sensor->mbus.flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
			signal |= RKISP1_CIF_ISP_ACQ_PROP_VSYNC_LOW;

		if (sensor->mbus.flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
			signal |= RKISP1_CIF_ISP_ACQ_PROP_HSYNC_LOW;
	}

	rkisp1_write(rkisp1, isp_ctrl, RKISP1_CIF_ISP_CTRL);
	rkisp1_write(rkisp1, signal | in_fmt->yuv_seq |
		     RKISP1_CIF_ISP_ACQ_PROP_BAYER_PAT(in_fmt->bayer_pat) |
		     RKISP1_CIF_ISP_ACQ_PROP_FIELD_SEL_ALL,
		     RKISP1_CIF_ISP_ACQ_PROP);
	rkisp1_write(rkisp1, 0, RKISP1_CIF_ISP_ACQ_NR_FRAMES);

	/* Acquisition Size */
	rkisp1_write(rkisp1, 0, RKISP1_CIF_ISP_ACQ_H_OFFS);
	rkisp1_write(rkisp1, 0, RKISP1_CIF_ISP_ACQ_V_OFFS);
	rkisp1_write(rkisp1,
		     acq_mult * in_frm->width, RKISP1_CIF_ISP_ACQ_H_SIZE);
	rkisp1_write(rkisp1, in_frm->height, RKISP1_CIF_ISP_ACQ_V_SIZE);

	/* ISP Out Area */
	rkisp1_write(rkisp1, in_crop->left, RKISP1_CIF_ISP_OUT_H_OFFS);
	rkisp1_write(rkisp1, in_crop->top, RKISP1_CIF_ISP_OUT_V_OFFS);
	rkisp1_write(rkisp1, in_crop->width, RKISP1_CIF_ISP_OUT_H_SIZE);
	rkisp1_write(rkisp1, in_crop->height, RKISP1_CIF_ISP_OUT_V_SIZE);

	/* interrupt mask */
	irq_mask |= RKISP1_CIF_ISP_FRAME | RKISP1_CIF_ISP_V_START |
		    RKISP1_CIF_ISP_PIC_SIZE_ERROR | RKISP1_CIF_ISP_FRAME_IN;
	rkisp1_write(rkisp1, irq_mask, RKISP1_CIF_ISP_IMSC);

	if (out_fmt->fmt_type == RKISP1_FMT_BAYER) {
		rkisp1_params_disable_isp(&rkisp1->params);
	} else {
		struct v4l2_mbus_framefmt *out_frm;

		out_frm = rkisp1_isp_sd_get_pad_fmt(&rkisp1->isp_sdev, NULL,
						    RKISP1_ISP_PAD_SINK_VIDEO,
						    V4L2_SUBDEV_FORMAT_ACTIVE);
		rkisp1_params_configure_isp(&rkisp1->params, in_fmt,
					    out_frm->quantization);
	}

	return 0;
}

static int rkisp1_config_dvp(struct rkisp1_device *rkisp1)
{
	const struct rkisp1_fmt *in_fmt = rkisp1->isp_sdev.in_fmt;
	u32 val, input_sel;

	// TODO: bus_w move info to core
	switch (in_fmt->bus_width) {
	case 8:
		input_sel = RKISP1_CIF_ISP_ACQ_PROP_IN_SEL_8B_ZERO;
		break;
	case 10:
		input_sel = RKISP1_CIF_ISP_ACQ_PROP_IN_SEL_10B_ZERO;
		break;
	case 12:
		input_sel = RKISP1_CIF_ISP_ACQ_PROP_IN_SEL_12B;
		break;
	default:
		dev_err(rkisp1->dev, "Invalid bus width\n");
		return -EINVAL;
	}

	val = rkisp1_read(rkisp1, RKISP1_CIF_ISP_ACQ_PROP);
	rkisp1_write(rkisp1, val | input_sel, RKISP1_CIF_ISP_ACQ_PROP);

	return 0;
}

static int rkisp1_config_mipi(struct rkisp1_device *rkisp1)
{
	const struct rkisp1_fmt *in_fmt = rkisp1->isp_sdev.in_fmt;
	unsigned int lanes;
	u32 mipi_ctrl;

	/*
	 * rkisp1->active_sensor->mbus is set in isp or d-phy notifier_bound
	 * function
	 */
	switch (rkisp1->active_sensor->mbus.flags & V4L2_MBUS_CSI2_LANES) {
	case V4L2_MBUS_CSI2_4_LANE:
		lanes = 4;
		break;
	case V4L2_MBUS_CSI2_3_LANE:
		lanes = 3;
		break;
	case V4L2_MBUS_CSI2_2_LANE:
		lanes = 2;
		break;
	case V4L2_MBUS_CSI2_1_LANE:
		lanes = 1;
		break;
	default:
		return -EINVAL;
	}

	mipi_ctrl = RKISP1_CIF_MIPI_CTRL_NUM_LANES(lanes - 1) |
		    RKISP1_CIF_MIPI_CTRL_SHUTDOWNLANES(0xf) |
		    RKISP1_CIF_MIPI_CTRL_ERR_SOT_SYNC_HS_SKIP |
		    RKISP1_CIF_MIPI_CTRL_CLOCKLANE_ENA;

	rkisp1_write(rkisp1, mipi_ctrl, RKISP1_CIF_MIPI_CTRL);

	/* Configure Data Type and Virtual Channel */
	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_DATA_SEL_DT(in_fmt->mipi_dt) |
		     RKISP1_CIF_MIPI_DATA_SEL_VC(0),
		     RKISP1_CIF_MIPI_IMG_DATA_SEL);

	/* Clear MIPI interrupts */
	rkisp1_write(rkisp1, ~0, RKISP1_CIF_MIPI_ICR);
	/*
	 * Disable RKISP1_CIF_MIPI_ERR_DPHY interrupt here temporary for
	 * isp bus may be dead when switch isp.
	 */
	rkisp1_write(rkisp1,
		     RKISP1_CIF_MIPI_FRAME_END | RKISP1_CIF_MIPI_ERR_CSI |
		     RKISP1_CIF_MIPI_ERR_DPHY |
		     RKISP1_CIF_MIPI_SYNC_FIFO_OVFLW(0x03) |
		     RKISP1_CIF_MIPI_ADD_DATA_OVFLW,
		     RKISP1_CIF_MIPI_IMSC);

	dev_dbg(rkisp1->dev, "\n  MIPI_CTRL 0x%08x\n"
		"  MIPI_IMG_DATA_SEL 0x%08x\n"
		"  MIPI_STATUS 0x%08x\n"
		"  MIPI_IMSC 0x%08x\n",
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_CTRL),
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_IMG_DATA_SEL),
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_STATUS),
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_IMSC));

	return 0;
}

/* Configure MUX */
static int rkisp1_config_path(struct rkisp1_device *rkisp1)
{
	struct rkisp1_sensor_async *sensor = rkisp1->active_sensor;
	u32 dpcl = rkisp1_read(rkisp1, RKISP1_CIF_VI_DPCL);
	int ret = 0;

	if (sensor->mbus.type == V4L2_MBUS_BT656 ||
	    sensor->mbus.type == V4L2_MBUS_PARALLEL) {
		ret = rkisp1_config_dvp(rkisp1);
		dpcl |= RKISP1_CIF_VI_DPCL_IF_SEL_PARALLEL;
	} else if (sensor->mbus.type == V4L2_MBUS_CSI2_DPHY) {
		ret = rkisp1_config_mipi(rkisp1);
		dpcl |= RKISP1_CIF_VI_DPCL_IF_SEL_MIPI;
	}

	rkisp1_write(rkisp1, dpcl, RKISP1_CIF_VI_DPCL);

	return ret;
}

/* Hardware configure Entry */
static int rkisp1_config_cif(struct rkisp1_device *rkisp1)
{
	u32 cif_id;
	int ret;

	dev_dbg(rkisp1->dev, "SP streaming = %d, MP streaming = %d\n",
		rkisp1->capture_devs[RKISP1_CAPTURE_SP].streaming,
		rkisp1->capture_devs[RKISP1_CAPTURE_MP].streaming);

	cif_id = rkisp1_read(rkisp1, RKISP1_CIF_VI_ID);
	dev_dbg(rkisp1->dev, "CIF_ID 0x%08x\n", cif_id);

	ret = rkisp1_config_isp(rkisp1);
	if (ret < 0)
		return ret;
	ret = rkisp1_config_path(rkisp1);
	if (ret < 0)
		return ret;
	rkisp1_config_ism(rkisp1);

	return 0;
}

/* Mess register operations to stop ISP */
static int rkisp1_isp_stop(struct rkisp1_device *rkisp1)
{
	u32 val;

	dev_dbg(rkisp1->dev, "SP streaming = %d, MP streaming = %d\n",
		rkisp1->capture_devs[RKISP1_CAPTURE_SP].streaming,
		rkisp1->capture_devs[RKISP1_CAPTURE_MP].streaming);

	/*
	 * ISP(mi) stop in mi frame end -> Stop ISP(mipi) ->
	 * Stop ISP(isp) ->wait for ISP isp off
	 */
	/* stop and clear MI, MIPI, and ISP interrupts */
	rkisp1_write(rkisp1, 0, RKISP1_CIF_MIPI_IMSC);
	rkisp1_write(rkisp1, ~0, RKISP1_CIF_MIPI_ICR);

	rkisp1_write(rkisp1, 0, RKISP1_CIF_ISP_IMSC);
	rkisp1_write(rkisp1, ~0, RKISP1_CIF_ISP_ICR);

	rkisp1_write(rkisp1, 0, RKISP1_CIF_MI_IMSC);
	rkisp1_write(rkisp1, ~0, RKISP1_CIF_MI_ICR);
	val = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_CTRL);
	rkisp1_write(rkisp1, val & (~RKISP1_CIF_MIPI_CTRL_OUTPUT_ENA),
		     RKISP1_CIF_MIPI_CTRL);
	/* stop ISP */
	val = rkisp1_read(rkisp1, RKISP1_CIF_ISP_CTRL);
	val &= ~(RKISP1_CIF_ISP_CTRL_ISP_INFORM_ENABLE |
		 RKISP1_CIF_ISP_CTRL_ISP_ENABLE);
	rkisp1_write(rkisp1, val, RKISP1_CIF_ISP_CTRL);

	val = rkisp1_read(rkisp1,	RKISP1_CIF_ISP_CTRL);
	rkisp1_write(rkisp1, val | RKISP1_CIF_ISP_CTRL_ISP_CFG_UPD,
		     RKISP1_CIF_ISP_CTRL);

	readx_poll_timeout(readl, rkisp1->base_addr + RKISP1_CIF_ISP_RIS,
			   val, val & RKISP1_CIF_ISP_OFF, 20, 100);
	dev_dbg(rkisp1->dev,
		"streaming(MP:%d, SP:%d), MI_CTRL:%x, ISP_CTRL:%x, MIPI_CTRL:%x\n",
		rkisp1->capture_devs[RKISP1_CAPTURE_SP].streaming,
		rkisp1->capture_devs[RKISP1_CAPTURE_MP].streaming,
		rkisp1_read(rkisp1, RKISP1_CIF_MI_CTRL),
		rkisp1_read(rkisp1, RKISP1_CIF_ISP_CTRL),
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_CTRL));

	rkisp1_write(rkisp1,
		     RKISP1_CIF_IRCL_MIPI_SW_RST | RKISP1_CIF_IRCL_ISP_SW_RST,
		     RKISP1_CIF_IRCL);
	rkisp1_write(rkisp1, 0x0, RKISP1_CIF_IRCL);

	return 0;
}

static void rkisp1_config_clk(struct rkisp1_device *rkisp1)
{
	u32 val = RKISP1_CIF_ICCL_ISP_CLK | RKISP1_CIF_ICCL_CP_CLK |
		  RKISP1_CIF_ICCL_MRSZ_CLK | RKISP1_CIF_ICCL_SRSZ_CLK |
		  RKISP1_CIF_ICCL_JPEG_CLK | RKISP1_CIF_ICCL_MI_CLK |
		  RKISP1_CIF_ICCL_IE_CLK | RKISP1_CIF_ICCL_MIPI_CLK |
		  RKISP1_CIF_ICCL_DCROP_CLK;

	rkisp1_write(rkisp1, val, RKISP1_CIF_ICCL);
}

/* Mess register operations to start ISP */
static int rkisp1_isp_start(struct rkisp1_device *rkisp1)
{
	struct rkisp1_sensor_async *sensor = rkisp1->active_sensor;
	u32 val;

	dev_dbg(rkisp1->dev, "SP streaming = %d, MP streaming = %d\n",
		rkisp1->capture_devs[RKISP1_CAPTURE_SP].streaming,
		rkisp1->capture_devs[RKISP1_CAPTURE_MP].streaming);

	rkisp1_config_clk(rkisp1);

	/* Activate MIPI */
	if (sensor->mbus.type == V4L2_MBUS_CSI2_DPHY) {
		val = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_CTRL);
		rkisp1_write(rkisp1, val | RKISP1_CIF_MIPI_CTRL_OUTPUT_ENA,
			     RKISP1_CIF_MIPI_CTRL);
	}
	/* Activate ISP */
	val = rkisp1_read(rkisp1, RKISP1_CIF_ISP_CTRL);
	val |= RKISP1_CIF_ISP_CTRL_ISP_CFG_UPD |
	       RKISP1_CIF_ISP_CTRL_ISP_ENABLE |
	       RKISP1_CIF_ISP_CTRL_ISP_INFORM_ENABLE;
	rkisp1_write(rkisp1, val, RKISP1_CIF_ISP_CTRL);

	/* XXX: Is the 1000us too long?
	 * CIF spec says to wait for sufficient time after enabling
	 * the MIPI interface and before starting the sensor output.
	 */
	usleep_range(1000, 1200);

	dev_dbg(rkisp1->dev,
		"SP streaming = %d, MP streaming = %d MI_CTRL 0x%08x\n"
		"  ISP_CTRL 0x%08x MIPI_CTRL 0x%08x\n",
		rkisp1->capture_devs[RKISP1_CAPTURE_SP].streaming,
		rkisp1->capture_devs[RKISP1_CAPTURE_MP].streaming,
		rkisp1_read(rkisp1, RKISP1_CIF_MI_CTRL),
		rkisp1_read(rkisp1, RKISP1_CIF_ISP_CTRL),
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_CTRL));

	return 0;
}

/***************************** ISP sub-devs *******************************/

static const struct rkisp1_fmt rkisp1_isp_formats[] = {
	{
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
		.fmt_type	= RKISP1_FMT_YUV,
		.direction	= RKISP1_DIR_OUT,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB10_1X10,
		.fmt_type	= RKISP1_FMT_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW10,
		.bayer_pat	= RKISP1_RAW_RGGB,
		// TODO: Move bus_width to a helper, with a note that it can be moved
		// to v4l2-common.c
		.bus_width	= 10,
		.direction	= RKISP1_DIR_IN_OUT,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.fmt_type	= RKISP1_FMT_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW10,
		.bayer_pat	= RKISP1_RAW_BGGR,
		.bus_width	= 10,
		.direction	= RKISP1_DIR_IN_OUT,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG10_1X10,
		.fmt_type	= RKISP1_FMT_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW10,
		.bayer_pat	= RKISP1_RAW_GBRG,
		.bus_width	= 10,
		.direction	= RKISP1_DIR_IN_OUT,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG10_1X10,
		.fmt_type	= RKISP1_FMT_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW10,
		.bayer_pat	= RKISP1_RAW_GRBG,
		.bus_width	= 10,
		.direction	= RKISP1_DIR_IN_OUT,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB12_1X12,
		.fmt_type	= RKISP1_FMT_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW12,
		.bayer_pat	= RKISP1_RAW_RGGB,
		.bus_width	= 12,
		.direction	= RKISP1_DIR_IN_OUT,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR12_1X12,
		.fmt_type	= RKISP1_FMT_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW12,
		.bayer_pat	= RKISP1_RAW_BGGR,
		.bus_width	= 12,
		.direction	= RKISP1_DIR_IN_OUT,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG12_1X12,
		.fmt_type	= RKISP1_FMT_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW12,
		.bayer_pat	= RKISP1_RAW_GBRG,
		.bus_width	= 12,
		.direction	= RKISP1_DIR_IN_OUT,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG12_1X12,
		.fmt_type	= RKISP1_FMT_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW12,
		.bayer_pat	= RKISP1_RAW_GRBG,
		.bus_width	= 12,
		.direction	= RKISP1_DIR_IN_OUT,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB8_1X8,
		.fmt_type	= RKISP1_FMT_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW8,
		.bayer_pat	= RKISP1_RAW_RGGB,
		.bus_width	= 8,
		.direction	= RKISP1_DIR_IN_OUT,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR8_1X8,
		.fmt_type	= RKISP1_FMT_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW8,
		.bayer_pat	= RKISP1_RAW_BGGR,
		.bus_width	= 8,
		.direction	= RKISP1_DIR_IN_OUT,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG8_1X8,
		.fmt_type	= RKISP1_FMT_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW8,
		.bayer_pat	= RKISP1_RAW_GBRG,
		.bus_width	= 8,
		.direction	= RKISP1_DIR_IN_OUT,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG8_1X8,
		.fmt_type	= RKISP1_FMT_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW8,
		.bayer_pat	= RKISP1_RAW_GRBG,
		.bus_width	= 8,
		.direction	= RKISP1_DIR_IN_OUT,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_1X16,
		.fmt_type	= RKISP1_FMT_YUV,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= RKISP1_CIF_ISP_ACQ_PROP_YCBYCR,
		.bus_width	= 16,
		.direction	= RKISP1_DIR_IN,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YVYU8_1X16,
		.fmt_type	= RKISP1_FMT_YUV,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= RKISP1_CIF_ISP_ACQ_PROP_YCRYCB,
		.bus_width	= 16,
		.direction	= RKISP1_DIR_IN,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_UYVY8_1X16,
		.fmt_type	= RKISP1_FMT_YUV,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= RKISP1_CIF_ISP_ACQ_PROP_CBYCRY,
		.bus_width	= 16,
		.direction	= RKISP1_DIR_IN,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_VYUY8_1X16,
		.fmt_type	= RKISP1_FMT_YUV,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= RKISP1_CIF_ISP_ACQ_PROP_CRYCBY,
		.bus_width	= 16,
		.direction	= RKISP1_DIR_IN,
	},
};

static const struct rkisp1_fmt *rkisp1_find_fmt(u32 mbus_code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rkisp1_isp_formats); i++) {
		const struct rkisp1_fmt *fmt = &rkisp1_isp_formats[i];

		if (fmt->mbus_code == mbus_code)
			return fmt;
	}

	return NULL;
}

static int rkisp1_isp_sd_enum_mbus_code(struct v4l2_subdev *sd,
					struct v4l2_subdev_pad_config *cfg,
					struct v4l2_subdev_mbus_code_enum *code)
{
	unsigned int i, dir;
	int pos = 0;

	if (code->pad == RKISP1_ISP_PAD_SINK_VIDEO) {
		dir = RKISP1_DIR_IN;
	} else if (code->pad == RKISP1_ISP_PAD_SOURCE_VIDEO) {
		dir = RKISP1_DIR_OUT;
	} else {
		if (code->index > 0)
			return -EINVAL;
		code->code = MEDIA_BUS_FMT_FIXED;
		return 0;
	}

	if (code->index >= ARRAY_SIZE(rkisp1_isp_formats))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(rkisp1_isp_formats); i++) {
		const struct rkisp1_fmt *fmt = &rkisp1_isp_formats[i];

		if (fmt->direction & dir)
			pos++;

		if (code->index == pos - 1) {
			code->code = fmt->mbus_code;
			return 0;
		}
	}

	return -EINVAL;
}

static int rkisp1_isp_sd_init_config(struct v4l2_subdev *sd,
				     struct v4l2_subdev_pad_config *cfg)
{
	struct v4l2_rect *mf_in_crop, *mf_out_crop;
	struct v4l2_mbus_framefmt *mf_in, *mf_out;

	mf_in = v4l2_subdev_get_try_format(sd, cfg, RKISP1_ISP_PAD_SINK_VIDEO);
	mf_in->width = RKISP1_DEFAULT_WIDTH;
	mf_in->height = RKISP1_DEFAULT_HEIGHT;
	mf_in->field = V4L2_FIELD_NONE;
	mf_in->code = RKISP1_DEF_SINK_PAD_FMT;

	mf_in_crop = v4l2_subdev_get_try_crop(sd, cfg,
					      RKISP1_ISP_PAD_SINK_VIDEO);
	mf_in_crop->width = RKISP1_DEFAULT_WIDTH;
	mf_in_crop->height = RKISP1_DEFAULT_HEIGHT;
	mf_in_crop->left = 0;
	mf_in_crop->top = 0;

	mf_out = v4l2_subdev_get_try_format(sd, cfg,
					    RKISP1_ISP_PAD_SOURCE_VIDEO);
	*mf_out = *mf_in;
	mf_out->code = RKISP1_DEF_SRC_PAD_FMT;
	mf_out->quantization = V4L2_QUANTIZATION_FULL_RANGE;

	mf_out_crop = v4l2_subdev_get_try_crop(sd, cfg,
					       RKISP1_ISP_PAD_SOURCE_VIDEO);
	*mf_out_crop = *mf_in_crop;

	mf_in = v4l2_subdev_get_try_format(sd, cfg, RKISP1_ISP_PAD_SINK_PARAMS);
	mf_out = v4l2_subdev_get_try_format(sd, cfg,
					    RKISP1_ISP_PAD_SOURCE_STATS);
	/*
	 * NOTE: setting a format here doesn't make much sense
	 * but v4l2-compliance complains
	 */
	mf_in->width = RKISP1_DEFAULT_WIDTH;
	mf_in->height = RKISP1_DEFAULT_HEIGHT;
	mf_in->field = V4L2_FIELD_NONE;
	mf_in->code = MEDIA_BUS_FMT_FIXED;
	*mf_out = *mf_in;

	return 0;
}

static void rkisp1_isp_sd_set_out_crop(struct rkisp1_isp_subdev *isp_sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_rect *r, unsigned int which)
{
	const struct v4l2_rect *in_crop;
	struct v4l2_rect *out_crop;

	out_crop = rkisp1_isp_sd_get_pad_crop(isp_sd, cfg,
					      RKISP1_ISP_PAD_SOURCE_VIDEO,
					      which);

	out_crop->left = ALIGN(r->left, 2);
	out_crop->width = ALIGN(r->width, 2);
	out_crop->top = r->top;
	out_crop->height = r->height;

	in_crop = rkisp1_isp_sd_get_pad_crop(isp_sd, cfg,
					     RKISP1_ISP_PAD_SINK_VIDEO, which);

	out_crop->left = clamp_t(u32, out_crop->left, 0, in_crop->width);
	out_crop->top = clamp_t(u32, out_crop->top, 0, in_crop->height);
	out_crop->width = clamp_t(u32, out_crop->width,
				  RKISP1_CIF_ISP_OUTPUT_W_MIN,
				  in_crop->width - out_crop->left);
	out_crop->height = clamp_t(u32, out_crop->height,
				   RKISP1_CIF_ISP_OUTPUT_H_MIN,
				   in_crop->height - out_crop->top);
}

static void rkisp1_isp_sd_set_out_fmt(struct rkisp1_isp_subdev *isp_sd,
				      struct v4l2_subdev_pad_config *cfg,
				      struct v4l2_mbus_framefmt *format,
				      unsigned int which)
{
	struct v4l2_mbus_framefmt *out_fmt;
	const struct rkisp1_fmt *rk_fmt;
	const struct v4l2_rect *in_crop;

	out_fmt = rkisp1_isp_sd_get_pad_fmt(isp_sd, cfg,
					    RKISP1_ISP_PAD_SOURCE_VIDEO,
					    which);
	in_crop = rkisp1_isp_sd_get_pad_crop(isp_sd, cfg,
					     RKISP1_ISP_PAD_SINK_VIDEO, which);

	/*
	 * TODO: check if other fields besides width/height/quantization are
	 * also configurable. If yes, then accept them from userspace.
	 */
	out_fmt->code = format->code;
	rk_fmt = rkisp1_find_fmt(out_fmt->code);
	if (!rk_fmt) {
		out_fmt->code = RKISP1_DEF_SRC_PAD_FMT;
		rk_fmt = rkisp1_find_fmt(out_fmt->code);
	}
	if (which == V4L2_SUBDEV_FORMAT_ACTIVE)
		isp_sd->out_fmt = rk_fmt;
	/* window size is set in s_selection */
	out_fmt->width  = in_crop->width;
	out_fmt->height = in_crop->height;
	/* TODO: validate quantization value */
	out_fmt->quantization = format->quantization;
	/* full range by default */
	if (!out_fmt->quantization)
		out_fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;

	*format = *out_fmt;
}

static void rkisp1_isp_sd_set_in_crop(struct rkisp1_isp_subdev *isp_sd,
				      struct v4l2_subdev_pad_config *cfg,
				      struct v4l2_rect *r, unsigned int which)
{
	struct v4l2_mbus_framefmt *in_fmt, *out_fmt;
	struct v4l2_rect *in_crop, *out_crop;

	in_crop = rkisp1_isp_sd_get_pad_crop(isp_sd, cfg,
					     RKISP1_ISP_PAD_SINK_VIDEO,
					     which);

	in_crop->left = ALIGN(r->left, 2);
	in_crop->width = ALIGN(r->width, 2);
	in_crop->top = r->top;
	in_crop->height = r->height;

	in_fmt = rkisp1_isp_sd_get_pad_fmt(isp_sd, cfg,
					   RKISP1_ISP_PAD_SINK_VIDEO, which);

	in_crop->left = clamp_t(u32, in_crop->left, 0, in_fmt->width);
	in_crop->top = clamp_t(u32, in_crop->top, 0, in_fmt->height);
	in_crop->width = clamp_t(u32, in_crop->width,
				 RKISP1_CIF_ISP_INPUT_W_MIN,
				 in_fmt->width - in_crop->left);
	in_crop->height = clamp_t(u32, in_crop->height,
				  RKISP1_CIF_ISP_INPUT_H_MIN,
				  in_fmt->height - in_crop->top);

	/* Update source crop and format */
	out_fmt = rkisp1_isp_sd_get_pad_fmt(isp_sd, cfg,
					    RKISP1_ISP_PAD_SOURCE_VIDEO, which);
	rkisp1_isp_sd_set_out_fmt(isp_sd, cfg, out_fmt, which);

	out_crop = rkisp1_isp_sd_get_pad_crop(isp_sd, cfg,
					      RKISP1_ISP_PAD_SOURCE_VIDEO,
					      which);
	rkisp1_isp_sd_set_out_crop(isp_sd, cfg, out_crop, which);
}

static void rkisp1_isp_sd_set_in_fmt(struct rkisp1_isp_subdev *isp_sd,
				     struct v4l2_subdev_pad_config *cfg,
				     struct v4l2_mbus_framefmt *format,
				     unsigned int which)
{
	struct v4l2_mbus_framefmt *in_fmt;
	const struct rkisp1_fmt *rk_fmt;
	struct v4l2_rect *in_crop;

	in_fmt = rkisp1_isp_sd_get_pad_fmt(isp_sd, cfg,
					   RKISP1_ISP_PAD_SINK_VIDEO, which);

	/*
	 * TODO: check if other fields besides width/height/quantization are
	 * also configurable. If yes, then accept them from userspace.
	 */
	in_fmt->code = format->code;
	rk_fmt = rkisp1_find_fmt(in_fmt->code);
	if (!rk_fmt) {
		in_fmt->code = RKISP1_DEF_SINK_PAD_FMT;
		rk_fmt = rkisp1_find_fmt(in_fmt->code);
	}
	if (which == V4L2_SUBDEV_FORMAT_ACTIVE)
		isp_sd->in_fmt = rk_fmt;
	in_fmt->width = clamp_t(u32, format->width,
				RKISP1_CIF_ISP_INPUT_W_MIN,
				RKISP1_CIF_ISP_INPUT_W_MAX);
	in_fmt->height = clamp_t(u32, format->height,
				 RKISP1_CIF_ISP_INPUT_H_MIN,
				 RKISP1_CIF_ISP_INPUT_H_MAX);

	*format = *in_fmt;

	/* Update sink crop */
	in_crop = rkisp1_isp_sd_get_pad_crop(isp_sd, cfg,
					     RKISP1_ISP_PAD_SINK_VIDEO, which);
	rkisp1_isp_sd_set_in_crop(isp_sd, cfg, in_crop, which);
}

static int rkisp1_isp_sd_get_fmt(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_format *fmt)
{
	struct rkisp1_isp_subdev *isp_sd = rkisp1_sd_to_isp_sd(sd);

	fmt->format = *rkisp1_isp_sd_get_pad_fmt(isp_sd, cfg, fmt->pad,
						 fmt->which);
	return 0;
}

static int rkisp1_isp_sd_set_fmt(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_format *fmt)
{
	struct rkisp1_isp_subdev *isp_sd = rkisp1_sd_to_isp_sd(sd);

	if (fmt->pad == RKISP1_ISP_PAD_SINK_VIDEO)
		rkisp1_isp_sd_set_in_fmt(isp_sd, cfg, &fmt->format, fmt->which);
	else if (fmt->pad == RKISP1_ISP_PAD_SOURCE_VIDEO)
		rkisp1_isp_sd_set_out_fmt(isp_sd, cfg, &fmt->format,
					  fmt->which);
	else
		fmt->format = *rkisp1_isp_sd_get_pad_fmt(isp_sd, cfg, fmt->pad,
							 fmt->which);

	return 0;
}

static int rkisp1_isp_sd_get_selection(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_selection *sel)
{
	struct rkisp1_isp_subdev *isp_sd = rkisp1_sd_to_isp_sd(sd);

	if (sel->pad != RKISP1_ISP_PAD_SOURCE_VIDEO &&
	    sel->pad != RKISP1_ISP_PAD_SINK_VIDEO)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		if (sel->pad == RKISP1_ISP_PAD_SINK_VIDEO) {
			struct v4l2_mbus_framefmt *__format;

			__format = rkisp1_isp_sd_get_pad_fmt(isp_sd, cfg,
							     sel->pad,
							     sel->which);
			sel->r.height = __format->height;
			sel->r.width = __format->width;
			sel->r.left = 0;
			sel->r.top = 0;
		} else {
			sel->r = *rkisp1_isp_sd_get_pad_crop(isp_sd, cfg,
						RKISP1_ISP_PAD_SINK_VIDEO,
						sel->which);
		}
		break;
	case V4L2_SEL_TGT_CROP:
		sel->r = *rkisp1_isp_sd_get_pad_crop(isp_sd, cfg, sel->pad,
						     sel->which);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rkisp1_isp_sd_set_selection(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_selection *sel)
{
	struct rkisp1_isp_subdev *isp_sd = rkisp1_sd_to_isp_sd(sd);
	struct rkisp1_device *rkisp1 = container_of(sd->v4l2_dev,
						    struct rkisp1_device,
						    v4l2_dev);

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	dev_dbg(rkisp1->dev, "%s: pad: %d sel(%d,%d)/%dx%d\n", __func__,
		sel->pad, sel->r.left, sel->r.top, sel->r.width, sel->r.height);

	if (sel->pad == RKISP1_ISP_PAD_SINK_VIDEO)
		rkisp1_isp_sd_set_in_crop(isp_sd, cfg, &sel->r, sel->which);
	else if (sel->pad == RKISP1_ISP_PAD_SOURCE_VIDEO)
		rkisp1_isp_sd_set_out_crop(isp_sd, cfg, &sel->r, sel->which);
	else
		return -EINVAL;

	return 0;
}

static int rkisp1_mipi_csi2_s_stream_start(struct rkisp1_isp_subdev *isp_sd,
					   struct rkisp1_sensor_async *sensor)
{
	union phy_configure_opts opts;
	struct phy_configure_opts_mipi_dphy *cfg = &opts.mipi_dphy;
	s64 pixel_clock;

	if (!sensor->pixel_rate_ctrl) {
		dev_warn(sensor->sd->dev, "No pixel rate control in subdev\n");
		return -EPIPE;
	}

	pixel_clock = v4l2_ctrl_g_ctrl_int64(sensor->pixel_rate_ctrl);
	if (!pixel_clock) {
		dev_err(sensor->sd->dev, "Invalid pixel rate value\n");
		return -EINVAL;
	}

	phy_mipi_dphy_get_default_config(pixel_clock, isp_sd->in_fmt->bus_width,
					 sensor->lanes, cfg);
	phy_set_mode(sensor->dphy, PHY_MODE_MIPI_DPHY);
	phy_configure(sensor->dphy, &opts);
	phy_power_on(sensor->dphy);

	return 0;
}

static void rkisp1_mipi_csi2_s_stream_stop(struct rkisp1_sensor_async *sensor)
{
	phy_power_off(sensor->dphy);
}

static int rkisp1_isp_sd_s_stream(struct v4l2_subdev *sd, int on)
{
	struct rkisp1_device *rkisp1 = container_of(sd->v4l2_dev,
						    struct rkisp1_device,
						    v4l2_dev);
	struct v4l2_subdev *sensor_sd;
	int ret = 0;

	if (!on) {
		ret = rkisp1_isp_stop(rkisp1);
		if (ret < 0)
			return ret;
		rkisp1_mipi_csi2_s_stream_stop(rkisp1->active_sensor);
		return 0;
	}

	sensor_sd = rkisp1_get_remote_sensor(sd);
	if (!sensor_sd)
		return -ENODEV;
	rkisp1->active_sensor = container_of(sensor_sd->asd,
					      struct rkisp1_sensor_async, asd);

	atomic_set(&rkisp1->isp_sdev.frm_sync_seq, 0);
	ret = rkisp1_config_cif(rkisp1);
	if (ret < 0)
		return ret;

	/* TODO: support other interfaces */
	if (rkisp1->active_sensor->mbus.type != V4L2_MBUS_CSI2_DPHY)
		return -EINVAL;

	ret = rkisp1_mipi_csi2_s_stream_start(&rkisp1->isp_sdev,
				       rkisp1->active_sensor);
	if (ret < 0)
		return ret;

	ret = rkisp1_isp_start(rkisp1);
	if (ret)
		rkisp1_mipi_csi2_s_stream_stop(rkisp1->active_sensor);

	return ret;
}

static int rkisp1_subdev_link_validate(struct media_link *link)
{
	if (link->sink->index == RKISP1_ISP_PAD_SINK_PARAMS)
		return 0;

	return v4l2_subdev_link_validate(link);
}

static int rkisp1_subdev_fmt_link_validate(struct v4l2_subdev *sd,
					struct media_link *link,
					struct v4l2_subdev_format *source_fmt,
					struct v4l2_subdev_format *sink_fmt)
{
	if (source_fmt->format.code != sink_fmt->format.code)
		return -EPIPE;

	/* Crop is available */
	if (source_fmt->format.width < sink_fmt->format.width ||
	    source_fmt->format.height < sink_fmt->format.height)
		return -EPIPE;

	return 0;
}

static void rkisp1_isp_queue_event_sof(struct rkisp1_isp_subdev *isp)
{
	struct v4l2_event event = {
		.type = V4L2_EVENT_FRAME_SYNC,
		.u.frame_sync.frame_sequence =
			atomic_inc_return(&isp->frm_sync_seq) - 1,
	};
	v4l2_event_queue(isp->sd.devnode, &event);
}

static int rkisp1_isp_sd_subs_evt(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				  struct v4l2_event_subscription *sub)
{
	if (sub->type != V4L2_EVENT_FRAME_SYNC)
		return -EINVAL;

	/* V4L2_EVENT_FRAME_SYNC doesn't require an id, so zero should be set */
	if (sub->id != 0)
		return -EINVAL;

	return v4l2_event_subscribe(fh, sub, 0, NULL);
}

static const struct v4l2_subdev_pad_ops rkisp1_isp_sd_pad_ops = {
	.enum_mbus_code = rkisp1_isp_sd_enum_mbus_code,
	.get_selection = rkisp1_isp_sd_get_selection,
	.set_selection = rkisp1_isp_sd_set_selection,
	.init_cfg = rkisp1_isp_sd_init_config,
	.get_fmt = rkisp1_isp_sd_get_fmt,
	.set_fmt = rkisp1_isp_sd_set_fmt,
	.link_validate = rkisp1_subdev_fmt_link_validate,
};

static const struct media_entity_operations rkisp1_isp_sd_media_ops = {
	.link_validate = rkisp1_subdev_link_validate,
};

static const struct v4l2_subdev_video_ops rkisp1_isp_sd_video_ops = {
	.s_stream = rkisp1_isp_sd_s_stream,
};

static const struct v4l2_subdev_core_ops rkisp1_isp_core_ops = {
	.subscribe_event = rkisp1_isp_sd_subs_evt,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_ops rkisp1_isp_sd_ops = {
	.core = &rkisp1_isp_core_ops,
	.video = &rkisp1_isp_sd_video_ops,
	.pad = &rkisp1_isp_sd_pad_ops,
};

int rkisp1_register_isp_subdev(struct rkisp1_device *rkisp1,
			       struct v4l2_device *v4l2_dev)
{
	struct media_pad *pads = rkisp1->isp_sdev.pads;
	struct v4l2_subdev *sd = &rkisp1->isp_sdev.sd;
	int ret;

	v4l2_subdev_init(sd, &rkisp1_isp_sd_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sd->entity.ops = &rkisp1_isp_sd_media_ops;
	strscpy(sd->name, "rkisp1-isp-subdev", sizeof(sd->name));

	pads[RKISP1_ISP_PAD_SINK_VIDEO].flags =
		MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;
	pads[RKISP1_ISP_PAD_SINK_PARAMS].flags = MEDIA_PAD_FL_SINK;
	pads[RKISP1_ISP_PAD_SOURCE_VIDEO].flags = MEDIA_PAD_FL_SOURCE;
	pads[RKISP1_ISP_PAD_SOURCE_STATS].flags = MEDIA_PAD_FL_SOURCE;
	rkisp1->isp_sdev.in_fmt = rkisp1_find_fmt(RKISP1_DEF_SINK_PAD_FMT);
	rkisp1->isp_sdev.out_fmt = rkisp1_find_fmt(RKISP1_DEF_SRC_PAD_FMT);
	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	ret = media_entity_pads_init(&sd->entity, RKISP1_ISP_PAD_MAX, pads);
	if (ret < 0)
		return ret;

	sd->owner = THIS_MODULE;
	v4l2_set_subdevdata(sd, rkisp1);

	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret < 0) {
		dev_err(sd->dev, "Failed to register isp subdev\n");
		goto err_cleanup_media_entity;
	}

	rkisp1_isp_sd_init_config(sd, rkisp1->isp_sdev.pad_cfg);
	return 0;

err_cleanup_media_entity:
	media_entity_cleanup(&sd->entity);

	return ret;
}

void rkisp1_unregister_isp_subdev(struct rkisp1_device *rkisp1)
{
	struct v4l2_subdev *sd = &rkisp1->isp_sdev.sd;

	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
}

/****************  Interrupter Handlers ****************/

void rkisp1_mipi_isr_thread(struct rkisp1_device *rkisp1)
{
	unsigned long lock_flags = 0;
	u32 val, status;

	spin_lock_irqsave(&rkisp1->irq_status_lock, lock_flags);
	status = rkisp1->irq_status_mipi;
	spin_unlock_irqrestore(&rkisp1->irq_status_lock, lock_flags);
	if (!status)
		return;

	/*
	 * Disable DPHY errctrl interrupt, because this dphy
	 * erctrl signal is asserted until the next changes
	 * of line state. This time is may be too long and cpu
	 * is hold in this interrupt.
	 */
	if (status & RKISP1_CIF_MIPI_ERR_CTRL(0x0f)) {
		val = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_IMSC);
		rkisp1_write(rkisp1, val & ~RKISP1_CIF_MIPI_ERR_CTRL(0x0f),
			     RKISP1_CIF_MIPI_IMSC);
		rkisp1->isp_sdev.dphy_errctrl_disabled = true;
	}

	/*
	 * Enable DPHY errctrl interrupt again, if mipi have receive
	 * the whole frame without any error.
	 */
	if (status == RKISP1_CIF_MIPI_FRAME_END) {
		/*
		 * Enable DPHY errctrl interrupt again, if mipi have receive
		 * the whole frame without any error.
		 */
		if (rkisp1->isp_sdev.dphy_errctrl_disabled) {
			val = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_IMSC);
			val |= RKISP1_CIF_MIPI_ERR_CTRL(0x0f);
			rkisp1_write(rkisp1, val, RKISP1_CIF_MIPI_IMSC);
			rkisp1->isp_sdev.dphy_errctrl_disabled = false;
		}
	} else {
		dev_warn(rkisp1->dev, "MIPI status error: 0x%08x\n", status);
	}
}

void rkisp1_isp_isr_thread(struct rkisp1_device *rkisp1)
{
	unsigned long lock_flags = 0;
	u32 status, isp_err;

	spin_lock_irqsave(&rkisp1->irq_status_lock, lock_flags);
	status = rkisp1->irq_status_isp;
	spin_unlock_irqrestore(&rkisp1->irq_status_lock, lock_flags);
	if (!status)
		return;

	/* start edge of v_sync */
	if (status & RKISP1_CIF_ISP_V_START)
		rkisp1_isp_queue_event_sof(&rkisp1->isp_sdev);

	if (status & RKISP1_CIF_ISP_PIC_SIZE_ERROR) {
		/* Clear pic_size_error */
		// TODO just keep an err counter and debugfs-it
		isp_err = rkisp1_read(rkisp1, RKISP1_CIF_ISP_ERR);
		dev_err(rkisp1->dev,
			"RKISP1_CIF_ISP_PIC_SIZE_ERROR (0x%08x)", isp_err);
		rkisp1_write(rkisp1, isp_err, RKISP1_CIF_ISP_ERR_CLR);
	} else if (status & RKISP1_CIF_ISP_DATA_LOSS) {
		/* data_loss */
		// TODO just keep an err counter and debugfs-it
		dev_err(rkisp1->dev, "RKISP1_CIF_ISP_DATA_LOSS\n");
	}

	if (status & RKISP1_CIF_ISP_FRAME) {
		u32 isp_ris;

		/* Frame In (ISP) */
		isp_ris = rkisp1_read(rkisp1, RKISP1_CIF_ISP_RIS);
		if (isp_ris & (RKISP1_CIF_ISP_AWB_DONE |
			       RKISP1_CIF_ISP_AFM_FIN |
			       RKISP1_CIF_ISP_EXP_END |
			       RKISP1_CIF_ISP_HIST_MEASURE_RDY))
			// TODO instead of calling isr_thread, i'd call it
			// stats_handle or something like that.
			// I like to reserve the name isr for the top level ISR itself.
			// but that's a matter of taste, naming isr to each
			// function that handles the interrupts can also help
			// to navigate the code i guess.
			rkisp1_stats_isr_thread(&rkisp1->stats, isp_ris);
	}

	/*
	 * Then update changed configs. Some of them involve
	 * lot of register writes. Do those only one per frame.
	 * Do the updates in the order of the processing flow.
	 */
	rkisp1_params_isr(rkisp1, status);
}
