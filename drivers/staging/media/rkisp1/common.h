/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Rockchip ISP1 Driver - Common definitions
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#ifndef _RKISP1_COMMON_H
#define _RKISP1_COMMON_H

#include <linux/clk.h>
#include <linux/mutex.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>

#include "regs.h"
#include "uapi/rkisp1-config.h"

/* TODO: FIXME: changing the default resolution to higher values causes the
 * stream to stall.
 * The capture node gets the crop bounds from the isp source pad crop size, but
 * if the user updates isp source pad crop size and start streaming, the capture
 * doesn't detect the new crop bounds, and if the isp source pad crop size is
 * smaller then the capture crop size, the stream doesn't work.
 */
#define RKISP1_DEFAULT_WIDTH		800
#define RKISP1_DEFAULT_HEIGHT		600

#define RKISP1_MAX_STREAM		2
#define RKISP1_STREAM_MP		0
#define RKISP1_STREAM_SP		1

#define RKISP1_PLANE_Y			0
#define RKISP1_PLANE_CB			1
#define RKISP1_PLANE_CR			2

// TODO If something is used locally, then don't "export it" in a header. I.e.
// move the define to where it's needed. This helps separate local vs. shared,
// private vs. exposed.
#define RKISP1_DRIVER_NAME	"rkisp1"
#define RKISP1_ISP_VDEV_NAME	RKISP1_DRIVER_NAME "_ispdev"
#define RKISP1_SP_VDEV_NAME	RKISP1_DRIVER_NAME "_selfpath"
#define RKISP1_MP_VDEV_NAME	RKISP1_DRIVER_NAME "_mainpath"
#define RKISP1_DMA_VDEV_NAME	RKISP1_DRIVER_NAME "_dmapath"

#define RKISP1_MAX_BUS_CLK	8

#define RKISP1_DIR_OUT BIT(0)
#define RKISP1_DIR_IN BIT(1)
#define RKISP1_DIR_IN_OUT (RKISP1_DIR_IN | RKISP1_DIR_OUT)

enum rkisp1_fmt_pix_type {
	RKISP1_FMT_YUV,
	RKISP1_FMT_RGB,
	RKISP1_FMT_BAYER,
	RKISP1_FMT_JPEG,
};

enum rkisp1_fmt_raw_pat_type {
	RKISP1_RAW_RGGB = 0,
	RKISP1_RAW_GRBG,
	RKISP1_RAW_GBRG,
	RKISP1_RAW_BGGR,
};

enum rkisp1_isp_pad {
	RKISP1_ISP_PAD_SINK_VIDEO,
	RKISP1_ISP_PAD_SINK_PARAMS,
	RKISP1_ISP_PAD_SOURCE_VIDEO,
	RKISP1_ISP_PAD_SOURCE_STATS,
	RKISP1_ISP_PAD_MAX
};

enum rkisp1_isp_readout_cmd {
	RKISP1_ISP_READOUT_MEAS,
	RKISP1_ISP_READOUT_META,
};

enum rkisp1_sp_inp {
	RKISP1_SP_INP_ISP,
	RKISP1_SP_INP_DMA_SP,
	RKISP1_SP_INP_MAX
};

/*
 * struct rkisp1_sensor_async - Sensor information
 * @mbus: media bus configuration
 */
struct rkisp1_sensor_async {
	struct v4l2_async_subdev asd;
	struct v4l2_mbus_config mbus;
	unsigned int lanes;
	struct v4l2_subdev *sd;
	struct v4l2_ctrl *pixel_rate_ctrl;
	struct phy *dphy;
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

/* One structure per video node */
struct rkisp1_vdev_node {
	struct vb2_queue buf_queue;
	struct mutex vlock; /* ioctl serialization mutex */
	struct video_device vdev;
	struct media_pad pad;
};

struct rkisp1_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head queue;
	union {
		u32 buff_addr[VIDEO_MAX_PLANES];
		void *vaddr[VIDEO_MAX_PLANES];
	};
};

struct rkisp1_dummy_buffer {
	void *vaddr;
	dma_addr_t dma_addr;
	u32 size;
};

struct rkisp1_stream_sp {
	int y_stride;
	enum rkisp1_sp_inp input_sel;
};

struct rkisp1_stream_mp {
	bool raw_enable;
};

struct rkisp1_device;

/*
 * struct rkisp1_stream - ISP capture video device
 *
 * @out_isp_fmt: output ISP format
 * @out_fmt: output buffer size
 * @dcrop: coordinates of dual-crop
 *
 * @vbq_lock: lock to protect buf_queue
 * @buf_queue: queued buffer list
 * @dummy_buf: dummy space to store dropped data
 *
 * rkisp1 use shadowsock registers, so it need two buffer at a time
 * @curr_buf: the buffer used for current frame
 * @next_buf: the buffer used for next frame
 */
struct rkisp1_stream {
	unsigned id:1;
	struct rkisp1_device *rkisp1;
	struct rkisp1_vdev_node vnode;
	const struct rkisp1_stream_fmt *out_isp_fmt;
	struct v4l2_pix_format_mplane out_fmt;
	struct v4l2_rect dcrop;
	struct rkisp1_streams_ops *ops;
	const struct rkisp1_stream_cfg *config;
	spinlock_t vbq_lock; /* protects buf_queue, curr_buf and next_buf */
	struct list_head buf_queue;
	struct rkisp1_dummy_buffer dummy_buf;
	struct rkisp1_buffer *curr_buf;
	struct rkisp1_buffer *next_buf;
	bool streaming;
	bool stopping;
	wait_queue_head_t done;
	union {
		struct rkisp1_stream_sp sp;
		struct rkisp1_stream_mp mp;
	} u;
};

/*
 * struct rkisp1_isp_stats_vdev - ISP Statistics device
 *
 * @irq_lock: buffer queue lock
 * @stat: stats buffer list
 * @readout_wq: workqueue for statistics information read
 */
struct rkisp1_isp_stats_vdev {
	struct rkisp1_vdev_node vnode;
	struct rkisp1_device *rkisp1;

	spinlock_t irq_lock;
	struct list_head stat;
	struct v4l2_format vdev_fmt;
	bool streamon;

	struct workqueue_struct *readout_wq;
	struct mutex wq_lock;
};

/*
 * struct rkisp1_isp_params_vdev - ISP input parameters device
 *
 * @cur_params: Current ISP parameters
 * @first_params: the first params should take effect immediately
 */
struct rkisp1_isp_params_vdev {
	struct rkisp1_vdev_node vnode;
	struct rkisp1_device *rkisp1;

	spinlock_t config_lock;
	struct list_head params;
	struct rkisp1_isp_params_cfg cur_params;
	struct v4l2_format vdev_fmt;
	bool streamon;
	bool first_params;

	enum v4l2_quantization quantization;
	enum rkisp1_fmt_raw_pat_type raw_type;
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
	struct rkisp1_sensor_async *active_sensor;
	struct rkisp1_isp_subdev isp_sdev;
	struct rkisp1_stream streams[RKISP1_MAX_STREAM];
	struct rkisp1_isp_stats_vdev stats_vdev;
	struct rkisp1_isp_params_vdev params_vdev;
	struct media_pipeline pipe;
	struct vb2_alloc_ctx *alloc_ctx;
	u32 irq_status_mi;
	u32 irq_status_isp;
	u32 irq_status_mipi;
	/* protects irq_status_* between irq handler and threads */
	spinlock_t irq_status_lock;
};

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

struct rkisp1_isp_readout_work {
	struct work_struct work;
	struct rkisp1_isp_stats_vdev *stats_vdev;

	unsigned int frame_id;
	unsigned int isp_ris;
	enum rkisp1_isp_readout_cmd readout;
	struct vb2_buffer *vb;
};

static inline struct rkisp1_vdev_node *
rkisp1_vdev_to_node(struct video_device *vdev)
{
	return container_of(vdev, struct rkisp1_vdev_node, vdev);
}

static inline struct rkisp1_vdev_node *rkisp1_queue_to_node(struct vb2_queue *q)
{
	return container_of(q, struct rkisp1_vdev_node, buf_queue);
}

static inline struct rkisp1_buffer *
rkisp1_to_rkisp1_buffer(struct vb2_v4l2_buffer *vb)
{
	return container_of(vb, struct rkisp1_buffer, vb);
}

static inline void
rkisp1_write(struct rkisp1_device *rkisp1, u32 val, unsigned int addr)
{
	writel(val, rkisp1->base_addr + addr);
}

static inline u32 rkisp1_read(struct rkisp1_device *rkisp1, unsigned int addr)
{
	return readl(rkisp1->base_addr + addr);
}

struct v4l2_mbus_framefmt *
rkisp1_isp_sd_get_pad_fmt(struct rkisp1_isp_subdev *isp_sd,
			  struct v4l2_subdev_pad_config *cfg,
			  unsigned int pad, u32 which);

struct v4l2_rect *rkisp1_isp_sd_get_pad_crop(struct rkisp1_isp_subdev *isp_sd,
					     struct v4l2_subdev_pad_config *cfg,
					     unsigned int pad, u32 which);

int rkisp1_register_isp_subdev(struct rkisp1_device *rkisp1,
			       struct v4l2_device *v4l2_dev);

void rkisp1_unregister_isp_subdev(struct rkisp1_device *rkisp1);

void rkisp1_mipi_isr_thread(struct rkisp1_device *rkisp1);

void rkisp1_isp_isr_thread(struct rkisp1_device *rkisp1);

void rkisp1_unregister_stream_vdevs(struct rkisp1_device *rkisp1);
int rkisp1_register_stream_vdevs(struct rkisp1_device *rkisp1);
void rkisp1_stream_isr_thread(struct rkisp1_device *rkisp1);
void rkisp1_stream_init(struct rkisp1_device *rkisp1, u32 id);

void rkisp1_stats_isr_thread(struct rkisp1_isp_stats_vdev *stats_vdev,
			     u32 isp_ris);
int rkisp1_register_stats_vdev(struct rkisp1_isp_stats_vdev *stats_vdev,
			       struct v4l2_device *v4l2_dev,
			       struct rkisp1_device *rkisp1);
void rkisp1_unregister_stats_vdev(struct rkisp1_isp_stats_vdev *stats_vdev);

/* config params before ISP streaming */
void rkisp1_params_configure_isp(struct rkisp1_isp_params_vdev *params_vdev,
				 const struct rkisp1_fmt *in_fmt,
				 enum v4l2_quantization quantization);
void rkisp1_params_disable_isp(struct rkisp1_isp_params_vdev *params_vdev);

int rkisp1_register_params_vdev(struct rkisp1_isp_params_vdev *params_vdev,
				struct v4l2_device *v4l2_dev,
				struct rkisp1_device *rkisp1);

void rkisp1_unregister_params_vdev(struct rkisp1_isp_params_vdev *params_vdev);

void rkisp1_params_isr(struct rkisp1_device *rkisp1, u32 isp_mis);


#endif /* _RKISP1_COMMON_H */
