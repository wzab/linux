/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Rockchip ISP1 Driver - Common definitions
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#ifndef _RKISP1_COMMON_H
#define _RKISP1_COMMON_H

#include <linux/mutex.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>

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

/* One structure per video node */
struct rkisp1_vdev_node {
	struct vb2_queue buf_queue;
	struct mutex vlock; /* ioctl serialization mutex */
	struct video_device vdev;
	struct media_pad pad;
};

enum rkisp1_fmt_pix_type {
	RKISP1_FMT_YUV,
	RKISP1_FMT_RGB,
	RKISP1_FMT_BAYER,
	RKISP1_FMT_JPEG,
	RKISP1_FMT_MAX
};

enum rkisp1_fmt_raw_pat_type {
	RKISP1_RAW_RGGB = 0,
	RKISP1_RAW_GRBG,
	RKISP1_RAW_GBRG,
	RKISP1_RAW_BGGR,
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

static inline
struct rkisp1_vdev_node *rkisp1_vdev_to_node(struct video_device *vdev)
{
	return container_of(vdev, struct rkisp1_vdev_node, vdev);
}

static inline struct rkisp1_vdev_node *rkisp1_queue_to_node(struct vb2_queue *q)
{
	return container_of(q, struct rkisp1_vdev_node, buf_queue);
}

static inline
struct rkisp1_buffer *rkisp1_to_rkisp1_buffer(struct vb2_v4l2_buffer *vb)
{
	return container_of(vb, struct rkisp1_buffer, vb);
}

#endif /* _RKISP1_COMMON_H */
