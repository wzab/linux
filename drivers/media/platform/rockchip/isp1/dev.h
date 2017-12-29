// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip isp1 driver
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

#define GRP_ID_SENSOR			BIT(0)
#define GRP_ID_MIPIPHY			BIT(1)
#define GRP_ID_ISP			BIT(2)
#define GRP_ID_ISP_MP			BIT(3)
#define GRP_ID_ISP_SP			BIT(4)

#define RKISP1_MAX_BUS_CLK	8
#define RKISP1_MAX_SENSOR	2
#define RKISP1_MAX_PIPELINE	4

/*
 * struct rkisp1_pipeline - An ISP hardware pipeline
 *
 * Capture device call other devices via pipeline
 *
 * @num_subdevs: number of linked subdevs
 * @power_cnt: pipeline power count
 * @stream_cnt: stream power count
 */
struct rkisp1_pipeline {
	struct media_pipeline pipe;
	int num_subdevs;
	atomic_t power_cnt;
	atomic_t stream_cnt;
	struct v4l2_subdev *subdevs[RKISP1_MAX_PIPELINE];
	int (*open)(struct rkisp1_pipeline *p,
		    struct media_entity *me, bool prepare);
	int (*close)(struct rkisp1_pipeline *p);
	int (*set_stream)(struct rkisp1_pipeline *p, bool on);
};

/*
 * struct rkisp1_sensor - Sensor information
 * @mbus: media bus configuration
 */
struct rkisp1_sensor {
	struct v4l2_subdev *sd;
	struct v4l2_mbus_config mbus;
	unsigned int lanes;
	struct phy *dphy;
	struct list_head list;
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
	struct devfreq *devfreq;
	struct devfreq_event_dev *devfreq_event_dev;
	unsigned int clk_size;
	struct clk_bulk_data clks[RKISP1_MAX_BUS_CLK];
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct media_device media_dev;
	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *subdevs[RKISP1_SD_MAX];
	struct rkisp1_sensor *active_sensor;
	struct list_head sensors;
	struct rkisp1_isp_subdev isp_sdev;
	struct rkisp1_stream stream[RKISP1_MAX_STREAM];
	struct rkisp1_isp_stats_vdev stats_vdev;
	struct rkisp1_isp_params_vdev params_vdev;
	struct rkisp1_pipeline pipe;
	struct vb2_alloc_ctx *alloc_ctx;
};

#endif
