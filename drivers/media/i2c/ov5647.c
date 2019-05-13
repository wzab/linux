/*
 * A V4L2 driver for OmniVision OV5647 cameras.
 *
 * Based on Samsung S5K6AAFX SXGA 1/6" 1.3M CMOS Image Sensor driver
 * Copyright (C) 2011 Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * Based on Omnivision OV7670 Camera Driver
 * Copyright (C) 2006-7 Jonathan Corbet <corbet@lwn.net>
 *
 * Copyright (C) 2016, Synopsys, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-ctrls.h>

#define SENSOR_NAME "ov5647"

#define REG_NULL 0xffff

#define MIPI_CTRL00_CLOCK_LANE_GATE		BIT(5)
#define MIPI_CTRL00_BUS_IDLE			BIT(2)
#define MIPI_CTRL00_CLOCK_LANE_DISABLE		BIT(0)

#define OV5647_SW_STANDBY		0x0100
#define OV5647_SW_RESET			0x0103
#define OV5647_REG_CHIPID_H		0x300a
#define OV5647_REG_CHIPID_L		0x300b
#define OV5647_REG_PAD_OUT		0x300d
#define OV5647_REG_FRAME_OFF_NUMBER	0x4202
#define OV5647_REG_MIPI_CTRL00		0x4800
#define OV5647_REG_MIPI_CTRL14		0x4814

#define REG_TERM 0xfffe
#define VAL_TERM 0xfe
#define REG_DLY  0xffff

#define OV5647_ROW_START		0x01
#define OV5647_ROW_START_MIN		0
#define OV5647_ROW_START_MAX		2004
#define OV5647_ROW_START_DEF		54

#define OV5647_COLUMN_START		0x02
#define OV5647_COLUMN_START_MIN		0
#define OV5647_COLUMN_START_MAX		2750
#define OV5647_COLUMN_START_DEF		16

#define OV5647_WINDOW_HEIGHT		0x03
#define OV5647_WINDOW_HEIGHT_MIN	2
#define OV5647_WINDOW_HEIGHT_MAX	2006
#define OV5647_WINDOW_HEIGHT_DEF	1944

#define OV5647_WINDOW_WIDTH		0x04
#define OV5647_WINDOW_WIDTH_MIN		2
#define OV5647_WINDOW_WIDTH_MAX		2752
#define OV5647_WINDOW_WIDTH_DEF		2592

struct regval_list {
	u16 addr;
	u8 data;
};

struct ov5647_mode {
	unsigned int width;
	unsigned int height;
	unsigned int max_fps;
	unsigned int hts_def;
	unsigned int vts_def;
	const struct regval_list *reg_list;
};

struct ov5647 {
	struct v4l2_subdev		sd;
	struct media_pad		pad;
	struct mutex			lock;
	struct v4l2_mbus_framefmt	format;
	unsigned int			width;
	unsigned int			height;
	int				power_count;
	struct clk			*xclk;
	struct v4l2_ctrl		*pixel_rate;
	const struct ov5647_mode	*cur_mode;
	struct v4l2_ctrl_handler ctrl_handler;
	struct gpio_desc *enable_gpio;
};

static inline struct ov5647 *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov5647, sd);
}

static struct regval_list sensor_oe_disable_regs[] = {
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x00},
	{REG_NULL, 0x00}
};

static struct regval_list sensor_oe_enable_regs[] = {
	{0x3000, 0x0f},
	{0x3001, 0xff},
	{0x3002, 0xe4},
	{REG_NULL, 0x00}
};

static struct regval_list ov5647_init_regs[] = {
	/* upstream */
	{0x370c, 0x0f},
	{0x5000, 0x06},
	{0x5003, 0x08},
	{0x5a00, 0x08},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x00},
	{0x301d, 0xf0},
	{0x3a18, 0x00},
	{0x3a19, 0xf8},
	{0x3c01, 0x80},
	{0x3b07, 0x0c},
	{0x3630, 0x2e},
	{0x3632, 0xe2},
	{0x3633, 0x23},
	{0x3634, 0x44},
	{0x3620, 0x64},
	{0x3621, 0xe0},
	{0x3600, 0x37},
	{0x3704, 0xa0},
	{0x3703, 0x5a},
	{0x3715, 0x78},
	{0x3717, 0x01},
	{0x3731, 0x02},
	{0x370b, 0x60},
	{0x3705, 0x1a},
	{0x3f05, 0x02},
	{0x3f06, 0x10},
	{0x3f01, 0x0a},
	{0x3a0f, 0x58},
	{0x3a10, 0x50},
	{0x3a1b, 0x58},
	{0x3a1e, 0x50},
	{0x3a11, 0x60},
	{0x3a1f, 0x28},
	{0x4001, 0x02},
	{0x4000, 0x09},
	{0x5001, 0x01},		/* auto white balance */
	{0x5002, 0x41},
	{0x3011, 0x62},
	/* mipi */
	{0x3016, 0x08},
	{0x3017, 0xe0},
	{0x3018, 0x44},
	{0x3034, 0x08},
	{0x3106, 0xf5},
	{REG_NULL, 0x00}
};

static struct regval_list ov5647_640x480[] = {
	{0x3035, 0x21},
	{0x3036, 0x60},
	{0x303c, 0x11},
	{0x3821, 0x07},
	{0x3820, 0x41},
	{0x3827, 0xec}, /* ? */
	{0x370c, 0x0f}, /* ? */
	{0x3612, 0x59},
	{0x3618, 0x00},
	{0x301c, 0xf8}, /* ? */
	{0x380c, 0x07}, /* 0768 */
	{0x380d, 0x68},
	{0x380e, 0x03}, /* 03d8 */
	{0x380f, 0xd8},
	{0x3814, 0x31},
	{0x3815, 0x31},
	{0x3708, 0x64},
	{0x3709, 0x52},
	{0x3808, 0x02}, /* width = 640 */
	{0x3809, 0x80},
	{0x380a, 0x01}, /* heigth = 480 */
	{0x380b, 0xE0},
	{0x3800, 0x00},	/* X Start */
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x0a},
	{0x3805, 0x3f},
	{0x3806, 0x07},
	{0x3807, 0xa1},
	{0x3811, 0x08},
	{0x3813, 0x02},
	{0x3636, 0x06},
	{0x3a08, 0x01},
	{0x3a09, 0x27},
	{0x3a0a, 0x00},
	{0x3a0b, 0xf6},
	{0x3a0d, 0x04},
	{0x3a0e, 0x03},
	{0x4004, 0x02},
	{0x4837, 0x24},
	{0x4050, 0x6e},
	{0x4051, 0x8f},
	{REG_NULL, 0x00}
};

static struct regval_list ov5647_1296x972[] = {
	{0x3035, 0x21},		/* PLL */
	{0x3036, 0x60},		/* PLL */
	{0x303c, 0x11},		/* PLL */
	{0x3821, 0x07},		/* ISP mirror on, Sensor mirror on, H bin on */
	{0x3820, 0x41},		/* ISP flip off, Sensor flip off, V bin on */
	{0x3612, 0x59},		/* analog control */
	{0x3618, 0x00},		/* analog control */
	{0x380c, 0x07},		/* HTS = 1896 */
	{0x380d, 0x68},		/* HTS */
	{0x380e, 0x05},		/* VTS = 1420 */
	{0x380f, 0x8c},		/* VTS */
	{0x3814, 0x31},		/* X INC */
	{0x3815, 0x31},		/* X INC */
	{0x3708, 0x64},		/* analog control */
	{0x3709, 0x52},		/* analog control */
	{0x3808, 0x05},		/* DVPHO = 1296 */
	{0x3809, 0x10},		/* DVPHO */
	{0x380a, 0x03},		/* DVPVO = 984 */
	{0x380b, 0xcc},		/* DVPVO */
	{0x3800, 0x00},		/* X Start */
	{0x3801, 0x08},		/* X Start */
	{0x3802, 0x00},		/* Y Start */
	{0x3803, 0x02},		/* Y Start */
	{0x3804, 0x0a},		/* X End */
	{0x3805, 0x37},		/* X End */
	{0x3806, 0x07},		/* Y End */
	{0x3807, 0xa1},		/* Y End */
	/* banding filter */
	{0x3a08, 0x01},		/* B50 */
	{0x3a09, 0x27},		/* B50 */
	{0x3a0a, 0x00},		/* B60 */
	{0x3a0b, 0xf6},		/* B60 */
	{0x3a0d, 0x04},		/* B60 max */
	{0x3a0e, 0x03},		/* B50 max */
	{0x4004, 0x02},		/* black line number */
	{0x4837, 0x24},		/* MIPI pclk period */
	{REG_NULL, 0x00}
};

static struct regval_list ov5647_2592x1944[] = {
	{0x3035, 0x21},
	{0x3036, 0x60},
	{0x303c, 0x11},
	{0x3612, 0x5b},
	{0x3618, 0x04},
	{0x380c, 0x0a},
	{0x380d, 0x8c},
	{0x380e, 0x07},
	{0x380f, 0xb6},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3708, 0x64},
	{0x3709, 0x12},
	{0x3808, 0x0a},
	{0x3809, 0x20},
	{0x380a, 0x07},
	{0x380b, 0x98},
	{0x3800, 0x00},
	{0x3801, 0x0c},
	{0x3802, 0x00},
	{0x3803, 0x04},
	{0x3804, 0x0a},
	{0x3805, 0x33},
	{0x3806, 0x07},
	{0x3807, 0xa3},
	{0x3a08, 0x01},
	{0x3a09, 0x28},
	{0x3a0a, 0x00},
	{0x3a0b, 0xf6},
	{0x3a0d, 0x08},
	{0x3a0e, 0x06},
	{0x4004, 0x04},
	{0x4837, 0x19},
	{REG_NULL, 0x00}
};

static const struct ov5647_mode supported_modes[] = {
	{
		.width = 640,
		.height = 480,
		.max_fps = 40, /* should be 60 but I get errors that way */
		.hts_def = 0x0768,
		.vts_def = 0x03d8,
		.reg_list = ov5647_640x480,
	},
	{
		.width = 1296,
		.height = 972,
		.max_fps = 25,
		.hts_def = 0x0768,
		.vts_def = 0x058c,
		.reg_list = ov5647_1296x972,
	},
	{
		.width = 2592,
		.height = 1944,
		.max_fps = 15,
		.hts_def = 0x0a8c,
		.vts_def = 0x07b6,
		.reg_list = ov5647_2592x1944,
	},
};

static int ov5647_write(struct v4l2_subdev *sd, u16 reg, u8 val)
{
	int ret;
	unsigned char data[3] = { reg >> 8, reg & 0xff, val};
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ret = i2c_master_send(client, data, 3);
	if (ret < 0)
		dev_err(&client->dev, "%s: i2c write error %d, reg: %x\n",
				__func__, ret, reg);

	return ret;
}

static int ov5647_read(struct v4l2_subdev *sd, u16 reg, u8 *val)
{
	int ret;
	unsigned char data_w[2] = { reg >> 8, reg & 0xff };
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ret = i2c_master_send(client, data_w, 2);
	if (ret < 0) {
		dev_err(&client->dev, "%s: i2c write error %d, reg: %x\n",
			__func__, ret, reg);
		return ret;
	}

	ret = i2c_master_recv(client, val, 1);
	if (ret < 0)
		dev_err(&client->dev, "%s: i2c read error %d, reg: %x\n",
				__func__, ret, reg);

	return ret;
}

static int ov5647_write_array(struct v4l2_subdev *sd,
			      const struct regval_list *regs)
{
	int i, ret;

	for (i = 0; regs[i].addr != REG_NULL; i++) {
		ret = ov5647_write(sd, regs[i].addr, regs[i].data);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int ov5647_set_sw_standby(struct v4l2_subdev *sd, bool standby)
{
	int ret;
	u8 rdval;

	ret = ov5647_read(sd, OV5647_SW_STANDBY, &rdval);
	if (ret < 0)
		return ret;

	if (standby)
		rdval &= ~0x01;
	else
		rdval |= 0x01;

	return ov5647_write(sd, OV5647_SW_STANDBY, rdval);
}

static int ov5647_set_virtual_channel(struct v4l2_subdev *sd, int channel)
{
	u8 channel_id;
	int ret;

	ret = ov5647_read(sd, OV5647_REG_MIPI_CTRL14, &channel_id);
	if (ret < 0)
		return ret;

	channel_id &= ~(3 << 6);
	return ov5647_write(sd, OV5647_REG_MIPI_CTRL14, channel_id | (channel << 6));
}

static int ov5647_stream_on(struct v4l2_subdev *sd)
{
	int ret;

	ret = ov5647_write(sd, OV5647_REG_MIPI_CTRL00, MIPI_CTRL00_BUS_IDLE);
	if (ret < 0)
		return ret;
	/* TODO: Might need to sleep here */

	ret = ov5647_write(sd, OV5647_REG_FRAME_OFF_NUMBER, 0x00);
	if (ret < 0)
		return ret;

	ret = ov5647_write(sd, OV5647_REG_PAD_OUT, 0x00);
	if (ret < 0)
		return ret;

	return ov5647_set_sw_standby(sd, false);
}

static int ov5647_stream_off(struct v4l2_subdev *sd)
{
	int ret;

	ret = ov5647_set_sw_standby(sd, true);
	if (ret < 0)
		return ret;

	ret = ov5647_write(sd, OV5647_REG_MIPI_CTRL00, MIPI_CTRL00_CLOCK_LANE_GATE
			   | MIPI_CTRL00_BUS_IDLE | MIPI_CTRL00_CLOCK_LANE_DISABLE);
	if (ret < 0)
		return ret;

	ret = ov5647_write(sd, OV5647_REG_FRAME_OFF_NUMBER, 0x0f);
	if (ret < 0)
		return ret;

	return ov5647_write(sd, OV5647_REG_PAD_OUT, 0x01);
}

static int __sensor_init(struct v4l2_subdev *sd)
{
	int ret;
	u8 resetval;
	struct ov5647 *sensor = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ret = ov5647_set_sw_standby(sd, true);
	if (ret < 0)
		return ret;

	ret = ov5647_write(sd, OV5647_SW_RESET, 0x01);
	if (ret < 0)
		return ret;
	msleep(50);

	ret = ov5647_write_array(sd, ov5647_init_regs);
	if (ret < 0) {
		dev_err(&client->dev, "write sensor default regs error\n");
		return ret;
	}

	ret = ov5647_write_array(sd, sensor->cur_mode->reg_list);
	if (ret < 0) {
		dev_err(&client->dev, "write sensor mode regs error\n");
		return ret;
	}

	ret = ov5647_set_virtual_channel(sd, 0);
	if (ret < 0)
		return ret;

	msleep(50);

	/*
	 * stream off to make the clock lane into LP-11 state.
	 */
	return ov5647_stream_off(sd);
}

static int ov5647_sensor_power(struct v4l2_subdev *sd, int on)
{
	int ret = 0;
	struct ov5647 *ov5647 = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	mutex_lock(&ov5647->lock);

	if (on && !ov5647->power_count)	{
		dev_dbg(&client->dev, "OV5647 power on\n");

		ret = clk_prepare_enable(ov5647->xclk);
		if (ret < 0) {
			dev_err(&client->dev, "clk prepare enable failed\n");
			goto out;
		}

		gpiod_set_value_cansleep(ov5647->enable_gpio, 1);

		ret = ov5647_write_array(sd, sensor_oe_enable_regs);
		if (ret < 0) {
			clk_disable_unprepare(ov5647->xclk);
			dev_err(&client->dev,
				"write sensor_oe_enable_regs error\n");
			goto out;
		}

		ret = __sensor_init(sd);
		if (ret < 0) {
			clk_disable_unprepare(ov5647->xclk);
			dev_err(&client->dev,
				"Camera not available, check Power\n");
			goto out;
		}
	} else if (!on && ov5647->power_count == 1) {
		dev_dbg(&client->dev, "OV5647 power off\n");

		ret = ov5647_write_array(sd, sensor_oe_disable_regs);
		if (ret < 0)
			dev_dbg(&client->dev, "disable oe failed\n");

		clk_disable_unprepare(ov5647->xclk);
		gpiod_set_value_cansleep(ov5647->enable_gpio, 0);
	}

	/* Update the power count. */
	ov5647->power_count += on ? 1 : -1;
	WARN_ON(ov5647->power_count < 0);

out:
	mutex_unlock(&ov5647->lock);

	return ret;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov5647_sensor_get_register(struct v4l2_subdev *sd,
				struct v4l2_dbg_register *reg)
{
	u8 val;
	int ret;

	ret = ov5647_read(sd, reg->reg & 0xff, &val);
	if (ret < 0)
		return ret;

	reg->val = val;
	reg->size = 1;

	return 0;
}

static int ov5647_sensor_set_register(struct v4l2_subdev *sd,
				const struct v4l2_dbg_register *reg)
{
	return ov5647_write(sd, reg->reg & 0xff, reg->val & 0xff);
}
#endif

/*
 * Subdev core operations registration
 */
static const struct v4l2_subdev_core_ops ov5647_subdev_core_ops = {
	.s_power		= ov5647_sensor_power,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register		= ov5647_sensor_get_register,
	.s_register		= ov5647_sensor_set_register,
#endif
};

static int ov5647_s_stream(struct v4l2_subdev *sd, int enable)
{
	if (enable)
		return ov5647_stream_on(sd);
	else
		return ov5647_stream_off(sd);
}

static const struct v4l2_subdev_video_ops ov5647_subdev_video_ops = {
	.s_stream =		ov5647_s_stream,
};

static int ov5647_get_reso_dist(const struct ov5647_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct ov5647_mode *
ov5647_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = ov5647_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int ov5647_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov5647 *ov5647 = to_state(sd);
	const struct ov5647_mode *mode;
	s64 pixel_rate;
	int ret = 0;

	mutex_lock(&ov5647->lock);

	mode = ov5647_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_SBGGR8_1X8;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
	} else {
		ov5647->cur_mode = mode;
		pixel_rate = mode->vts_def * mode->hts_def * mode->max_fps;
		ret = v4l2_ctrl_s_ctrl_int64(ov5647->pixel_rate,
					     pixel_rate);
		if (ret < 0)
			goto err_unlock;
	}

err_unlock:
	mutex_unlock(&ov5647->lock);

	return ret;
}

static int ov5647_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov5647 *ov5647 = to_state(sd);
	const struct ov5647_mode *mode = ov5647->cur_mode;

	mutex_lock(&ov5647->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SBGGR8_1X8;
		fmt->format.field = V4L2_FIELD_NONE;
	}

	mutex_unlock(&ov5647->lock);

	return 0;
}

static int ov5647_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SBGGR8_1X8;

	return 0;
}

static int ov5647_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SBGGR8_1X8)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static const struct v4l2_subdev_pad_ops ov5647_subdev_pad_ops = {
	.enum_mbus_code = ov5647_enum_mbus_code,
	.enum_frame_size = ov5647_enum_frame_sizes,
	.get_fmt = ov5647_get_fmt,
	.set_fmt = ov5647_set_fmt,
};

static const struct v4l2_subdev_ops ov5647_subdev_ops = {
	.core		= &ov5647_subdev_core_ops,
	.video		= &ov5647_subdev_video_ops,
	.pad		= &ov5647_subdev_pad_ops,
};

static int ov5647_detect(struct v4l2_subdev *sd)
{
	u8 read;
	int ret;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ret = ov5647_write(sd, OV5647_SW_RESET, 0x01);
	if (ret < 0)
		return ret;

	ret = ov5647_read(sd, OV5647_REG_CHIPID_H, &read);
	if (ret < 0)
		return ret;

	if (read != 0x56) {
		dev_err(&client->dev, "ID High expected 0x56 got %x", read);
		return -ENODEV;
	}

	ret = ov5647_read(sd, OV5647_REG_CHIPID_L, &read);
	if (ret < 0)
		return ret;

	if (read != 0x47) {
		dev_err(&client->dev, "ID Low expected 0x47 got %x", read);
		return -ENODEV;
	}

	return ov5647_write(sd, OV5647_SW_RESET, 0x00);
}

static int ov5647_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *format =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	struct v4l2_rect *crop =
				v4l2_subdev_get_try_crop(sd, fh->pad, 0);

	crop->left = OV5647_COLUMN_START_DEF;
	crop->top = OV5647_ROW_START_DEF;
	crop->width = OV5647_WINDOW_WIDTH_DEF;
	crop->height = OV5647_WINDOW_HEIGHT_DEF;

	format->code = MEDIA_BUS_FMT_SBGGR8_1X8;

	format->width = OV5647_WINDOW_WIDTH_DEF;
	format->height = OV5647_WINDOW_HEIGHT_DEF;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;

	return 0;
}

static const struct v4l2_subdev_internal_ops ov5647_subdev_internal_ops = {
	.open = ov5647_open,
};

static int ov5647_parse_dt(struct device_node *np)
{
	struct v4l2_fwnode_endpoint bus_cfg = { .bus_type = 0 };
	struct device_node *ep;

	int ret;

	ep = of_graph_get_next_endpoint(np, NULL);
	if (!ep)
		return -EINVAL;

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep), &bus_cfg);

	of_node_put(ep);
	return ret;
}

static int ov5647_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct ov5647 *sensor;
	struct v4l2_subdev *sd;
	struct v4l2_ctrl_handler *handler;
	struct device_node *np = client->dev.of_node;
	u32 xclk_freq;
	int i, ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;
	sensor->cur_mode = &supported_modes[0];

	if (IS_ENABLED(CONFIG_OF) && np) {
		ret = ov5647_parse_dt(np);
		if (ret) {
			dev_err(dev, "DT parsing error: %d\n", ret);
			return ret;
		}
	}

	/* get system clock (xclk) */
	sensor->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(sensor->xclk)) {
		dev_err(dev, "could not get xclk");
		return PTR_ERR(sensor->xclk);
	}

	xclk_freq = clk_get_rate(sensor->xclk);
	if (xclk_freq != 25000000) {
		dev_err(dev, "Unsupported clock frequency: %u\n", xclk_freq);
		return -EINVAL;
	}

	sensor->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->enable_gpio)) {
		dev_err(dev, "cannot get enable gpio\n");
		return PTR_ERR(sensor->enable_gpio);
	}
	msleep(10);

	mutex_init(&sensor->lock);

	sd = &sensor->sd;
	v4l2_i2c_subdev_init(sd, client, &ov5647_subdev_ops);

	handler = &sensor->ctrl_handler;
	ret = v4l2_ctrl_handler_init(handler, 1);
	if (ret)
		return ret;
	sensor->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
					V4L2_CID_PIXEL_RATE,
					1, INT_MAX, 1, 1);
	sensor->sd.ctrl_handler = handler;

	sensor->sd.internal_ops = &ov5647_subdev_internal_ops;
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sensor->pad);
	if (ret < 0)
		goto mutex_remove;

	for (i = 0; i < 3; i++) {
		ret = ov5647_detect(sd);
		if (ret < 0) {
			dev_err(&client->dev, "not detected!");
			goto error;
		}
		msleep(10);
	}

	ret = v4l2_async_register_subdev(sd);
	if (ret < 0)
		goto error;

	dev_dbg(dev, "OmniVision OV5647 camera driver probed\n");
	return 0;
error:
	media_entity_cleanup(&sd->entity);
mutex_remove:
	mutex_destroy(&sensor->lock);
	return ret;
}

static int ov5647_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5647 *ov5647 = to_state(sd);

	v4l2_async_unregister_subdev(&ov5647->sd);
	media_entity_cleanup(&ov5647->sd.entity);
	v4l2_device_unregister_subdev(sd);
	mutex_destroy(&ov5647->lock);

	return 0;
}

static const struct i2c_device_id ov5647_id[] = {
	{ "ov5647", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov5647_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov5647_of_match[] = {
	{ .compatible = "ovti,ov5647" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ov5647_of_match);
#endif

static struct i2c_driver ov5647_driver = {
	.driver = {
		.of_match_table = of_match_ptr(ov5647_of_match),
		.name	= SENSOR_NAME,
	},
	.probe		= ov5647_probe,
	.remove		= ov5647_remove,
	.id_table	= ov5647_id,
};

module_i2c_driver(ov5647_driver);

MODULE_AUTHOR("Ramiro Oliveira <roliveir@synopsys.com>");
MODULE_DESCRIPTION("A low-level driver for OmniVision ov5647 sensors");
MODULE_LICENSE("GPL v2");
