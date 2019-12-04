/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Rockchip ISP1 Driver - Base driver
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#ifndef _RKISP1_DEV_H
#define _RKISP1_DEV_H

#include <linux/clk.h>

#include "capture.h"
#include "rkisp1.h"
#include "isp_params.h"
#include "isp_stats.h"

#define DRIVER_NAME "rkisp1"
#define ISP_VDEV_NAME DRIVER_NAME  "_ispdev"
#define SP_VDEV_NAME DRIVER_NAME   "_selfpath"
#define MP_VDEV_NAME DRIVER_NAME   "_mainpath"
#define DMA_VDEV_NAME DRIVER_NAME  "_dmapath"

#define RKISP1_MAX_BUS_CLK	8

/*
 * struct sensor_async_subdev - Sensor information
 * @mbus: media bus configuration
 */
struct sensor_async_subdev {
	struct v4l2_async_subdev asd;
	struct v4l2_mbus_config mbus;
	unsigned int lanes;
	struct v4l2_subdev *sd;
	struct v4l2_ctrl *pixel_rate_ctrl;
	struct phy *dphy;
};

/*
 * struct rkisp1_device - ISP platform device
 * @base_addr: base register address
 * @active_sensor: sensor in-use, set when streaming on
 * @isp_sdev: ISP sub-device
 * @rkisp1_stream: capture video device
 * @stats_vdev: ISP statistics output device
 * @params_vdev: ISP input parameters device
 */
struct rkisp1_device {
	void __iomem *base_addr;
	int irq;
	struct device *dev;
	unsigned int clk_size;
	struct clk_bulk_data clks[RKISP1_MAX_BUS_CLK];
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct media_device media_dev;
	struct v4l2_async_notifier notifier;
	struct sensor_async_subdev *active_sensor;
	struct rkisp1_isp_subdev isp_sdev;
	struct rkisp1_stream streams[RKISP1_MAX_STREAM];
	struct rkisp1_isp_stats_vdev stats_vdev;
	struct rkisp1_isp_params_vdev params_vdev;
	struct media_pipeline pipe;
	struct vb2_alloc_ctx *alloc_ctx;
};

#endif
