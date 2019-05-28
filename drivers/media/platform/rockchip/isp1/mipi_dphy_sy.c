// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip MIPI Synopsys DPHY driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 */

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-mipi-dphy.h>
#include <linux/platform_device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

enum mipi_dphy_sy_pads {
	MIPI_DPHY_SY_PAD_SINK = 0,
	MIPI_DPHY_SY_PAD_SOURCE,
	MIPI_DPHY_SY_PADS_NUM,
};

struct sensor_async_subdev {
	struct v4l2_async_subdev asd;
	struct v4l2_mbus_config mbus;
	int lanes;
};

struct mipi_csi2_sensor {
	struct v4l2_subdev *sd;
	struct v4l2_mbus_config mbus;
	int lanes;
	struct list_head list;
};

struct mipi_csi2_priv {
	struct device *dev;
	struct v4l2_async_notifier notifier;
	struct v4l2_subdev sd;
	struct media_pad pads[MIPI_DPHY_SY_PADS_NUM];
	struct phy *dphy;
	struct list_head sensors;
	bool is_streaming;
};

static inline struct mipi_csi2_priv *to_csi2_priv(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct mipi_csi2_priv, sd);
}

static struct v4l2_subdev *get_remote_sensor(struct v4l2_subdev *sd)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;

	local = &sd->entity.pads[MIPI_DPHY_SY_PAD_SINK];
	remote = media_entity_remote_pad(local);
	if (!remote) {
		v4l2_warn(sd, "No link between csi2 and sensor\n");
		return NULL;
	}

	sensor_me = media_entity_remote_pad(local)->entity;
	return media_entity_to_v4l2_subdev(sensor_me);
}

static struct mipi_csi2_sensor *sd_to_sensor(struct mipi_csi2_priv *priv,
					    struct v4l2_subdev *sd)
{
	struct mipi_csi2_sensor *sensor;

	list_for_each_entry(sensor, &priv->sensors, list)
		if (sensor->sd == sd)
			return sensor;

	return NULL;
}

static int mipi_csi2_s_stream_start(struct v4l2_subdev *sd)
{
	struct mipi_csi2_priv *priv = to_csi2_priv(sd);
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct mipi_csi2_sensor *sensor = sd_to_sensor(priv, sensor_sd);
	union phy_configure_opts opts = { 0 };
	struct phy_configure_opts_mipi_dphy *cfg = &opts.mipi_dphy;
	struct v4l2_ctrl *pixel_rate;
	s64 pixel_clock;

	if (priv->is_streaming)
		return 0;

	if (!sensor_sd) {
		v4l2_err(sd, "Could not find sensor\n");
		return -EINVAL;
	}

	pixel_rate = v4l2_ctrl_find(sensor_sd->ctrl_handler, V4L2_CID_PIXEL_RATE);
	if (!pixel_rate) {
		v4l2_warn(sd, "No pixel rate control in subdev\n");
		return -EPIPE;
	}

	pixel_clock = v4l2_ctrl_g_ctrl_int64(pixel_rate);
	if (!pixel_clock) {
		v4l2_err(sd, "Invalid pixel rate value\n");
		return -EINVAL;
	}

	phy_init(priv->dphy);

	/* TODO: Get bpp from somehere */
	phy_mipi_dphy_get_default_config(pixel_clock, 8, sensor->lanes, cfg);

	phy_set_mode(priv->dphy, PHY_MODE_MIPI_DPHY);
	phy_configure(priv->dphy, &opts);
	phy_power_on(priv->dphy);

	priv->is_streaming = true;
	return 0;
}

static int mipi_csi2_s_stream_stop(struct v4l2_subdev *sd)
{
	struct mipi_csi2_priv *priv = to_csi2_priv(sd);

	phy_power_off(priv->dphy);
	phy_exit(priv->dphy);

	priv->is_streaming = false;

	return 0;
}

static int mipi_csi2_s_stream(struct v4l2_subdev *sd, int on)
{
	if (on)
		return mipi_csi2_s_stream_start(sd);
	else
		return mipi_csi2_s_stream_stop(sd);
}

static int mipi_csi2_g_mbus_config(struct v4l2_subdev *sd,
				  struct v4l2_mbus_config *config)
{
	struct mipi_csi2_priv *priv = to_csi2_priv(sd);
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct mipi_csi2_sensor *sensor;

	if (!sensor_sd) {
		v4l2_err(sd, "Could not find sensor\n");
		return -EINVAL;
	}

	sensor = sd_to_sensor(priv, sensor_sd);

	*config = sensor->mbus;

	return 0;
}

static int mipi_csi2_s_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}

/* dphy accepts all fmt/size from sensor */
static int mipi_csi2_get_set_fmt(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_format *fmt)
{
	struct v4l2_subdev *sensor = get_remote_sensor(sd);

	/*
	 * Do not allow format changes and just relay whatever
	 * set currently in the sensor.
	 */
	return v4l2_subdev_call(sensor, pad, get_fmt, NULL, fmt);
}

static const struct v4l2_subdev_pad_ops mipi_csi2_subdev_pad_ops = {
	.set_fmt = mipi_csi2_get_set_fmt,
	.get_fmt = mipi_csi2_get_set_fmt,
};

static const struct v4l2_subdev_core_ops mipi_csi2_core_ops = {
	.s_power = mipi_csi2_s_power,
};

static const struct v4l2_subdev_video_ops mipi_csi2_video_ops = {
	.g_mbus_config = mipi_csi2_g_mbus_config,
	.s_stream = mipi_csi2_s_stream,
};

static const struct v4l2_subdev_ops mipi_csi2_subdev_ops = {
	.core = &mipi_csi2_core_ops,
	.video = &mipi_csi2_video_ops,
	.pad = &mipi_csi2_subdev_pad_ops,
};

/* The .bound() notifier callback when a match is found */
static int
rockchip_mipi_csi2_notifier_bound(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *sd,
				 struct v4l2_async_subdev *asd)
{
	struct mipi_csi2_priv *priv = container_of(notifier,
						  struct mipi_csi2_priv,
						  notifier);
	struct sensor_async_subdev *s_asd = container_of(asd,
					struct sensor_async_subdev, asd);
	struct mipi_csi2_sensor *sensor;
	unsigned int pad, ret;

	sensor = devm_kzalloc(priv->dev, sizeof(*sensor), GFP_KERNEL);
	list_add(&sensor->list, &priv->sensors);

	sensor->lanes = s_asd->lanes;
	sensor->mbus = s_asd->mbus;
	sensor->sd = sd;

	for (pad = 0; pad < sensor->sd->entity.num_pads; pad++)
		if (sensor->sd->entity.pads[pad].flags & MEDIA_PAD_FL_SOURCE)
			break;

	if (pad == sensor->sd->entity.num_pads) {
		dev_err(priv->dev,
			"failed to find src pad for %s\n",
			sensor->sd->name);

		return -ENXIO;
	}

	ret = media_create_pad_link(
			&sensor->sd->entity, pad,
			&priv->sd.entity, MIPI_DPHY_SY_PAD_SINK,
			!list_is_first(&sensor->list, &priv->sensors) ?
						0 : MEDIA_LNK_FL_ENABLED);
	if (ret) {
		dev_err(priv->dev,
			"failed to create link for %s\n",
			sensor->sd->name);
		return ret;
	}

	return 0;
}

/* The .unbind callback */
static void
rockchip_mipi_csi2_notifier_unbind(struct v4l2_async_notifier *notifier,
				  struct v4l2_subdev *sd,
				  struct v4l2_async_subdev *asd)
{
	struct mipi_csi2_priv *priv = container_of(notifier,
						  struct mipi_csi2_priv,
						  notifier);
	struct mipi_csi2_sensor *sensor = sd_to_sensor(priv, sd);

	sensor->sd = NULL;
}

static const struct
v4l2_async_notifier_operations rockchip_mipi_csi2_async_ops = {
	.bound = rockchip_mipi_csi2_notifier_bound,
	.unbind = rockchip_mipi_csi2_notifier_unbind,
};

static int rockchip_mipi_csi2_fwnode_parse(struct device *dev,
					  struct v4l2_fwnode_endpoint *vep,
					  struct v4l2_async_subdev *asd)
{
	struct sensor_async_subdev *s_asd =
			container_of(asd, struct sensor_async_subdev, asd);
	struct v4l2_mbus_config *config = &s_asd->mbus;

	if (vep->bus_type != V4L2_MBUS_CSI2_DPHY) {
		dev_err(dev, "Only CSI2 bus type is currently supported\n");
		return -EINVAL;
	}

	if (vep->base.port != 0) {
		dev_err(dev, "The PHY has only port 0\n");
		return -EINVAL;
	}

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = vep->bus.mipi_csi2.flags;
	s_asd->lanes = vep->bus.mipi_csi2.num_data_lanes;

	switch (vep->bus.mipi_csi2.num_data_lanes) {
	case 1:
		config->flags |= V4L2_MBUS_CSI2_1_LANE;
		break;
	case 2:
		config->flags |= V4L2_MBUS_CSI2_2_LANE;
		break;
	case 3:
		config->flags |= V4L2_MBUS_CSI2_3_LANE;
		break;
	case 4:
		config->flags |= V4L2_MBUS_CSI2_4_LANE;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rockchip_mipi_csi2_media_init(struct mipi_csi2_priv *priv)
{
	int ret;

	priv->pads[MIPI_DPHY_SY_PAD_SOURCE].flags =
		MEDIA_PAD_FL_SOURCE | MEDIA_PAD_FL_MUST_CONNECT;
	priv->pads[MIPI_DPHY_SY_PAD_SINK].flags =
		MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;

	priv->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	ret = media_entity_pads_init(&priv->sd.entity,
				MIPI_DPHY_SY_PADS_NUM, priv->pads);
	if (ret < 0)
		return ret;

	v4l2_async_notifier_init(&priv->notifier);

	ret = v4l2_async_notifier_parse_fwnode_endpoints_by_port(
		priv->dev, &priv->notifier,
		sizeof(struct sensor_async_subdev), 0,
		rockchip_mipi_csi2_fwnode_parse);
	if (ret < 0)
		return ret;

	if (list_empty(&priv->notifier.asd_list))
		return -ENODEV;	/* no endpoint */

	priv->sd.subdev_notifier = &priv->notifier;
	priv->notifier.ops = &rockchip_mipi_csi2_async_ops;
	ret = v4l2_async_subdev_notifier_register(&priv->sd, &priv->notifier);
	if (ret) {
		dev_err(priv->dev,
			"failed to register async notifier : %d\n", ret);
		v4l2_async_notifier_cleanup(&priv->notifier);
		return ret;
	}

	return v4l2_async_register_subdev(&priv->sd);
}

static int rockchip_mipi_csi2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct v4l2_subdev *sd;
	struct mipi_csi2_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	INIT_LIST_HEAD(&priv->sensors);
	priv->dev = dev;
	priv->dphy = devm_phy_get(dev, "dphy");
	if (IS_ERR(priv->dphy)) {
		dev_err(dev, "Couldn't get the MIPI D-PHY\n");
		return PTR_ERR(priv->dphy);
	}

	sd = &priv->sd;
	v4l2_subdev_init(sd, &mipi_csi2_subdev_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "rockchip-mipi-csi-2");
	sd->dev = dev;

	platform_set_drvdata(pdev, &sd->entity);

	ret = rockchip_mipi_csi2_media_init(priv);
	if (ret < 0)
		return ret;

	return 0;
}

static int rockchip_mipi_csi2_remove(struct platform_device *pdev)
{
	struct media_entity *me = platform_get_drvdata(pdev);
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(me);

	media_entity_cleanup(&sd->entity);

	return 0;
}

static const struct of_device_id rockchip_mipi_csi2_match_id[] = {
       {
               .compatible = "rockchip,rk3399-mipi-csi-2",
       },
       {
               .compatible = "rockchip,rk3288-mipi-csi-2",
       },
       {}
};
MODULE_DEVICE_TABLE(of, rockchip_mipi_csi2_match_id);

static struct platform_driver rockchip_isp_mipi_csi2_driver = {
	.probe = rockchip_mipi_csi2_probe,
	.remove = rockchip_mipi_csi2_remove,
	.driver = {
			.name = "rockchip-mipi-csi-2",
			.of_match_table = rockchip_mipi_csi2_match_id,
	},
};

module_platform_driver(rockchip_isp_mipi_csi2_driver);
MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("Rockchip MIPI DPHY driver");
MODULE_LICENSE("Dual BSD/GPL");
