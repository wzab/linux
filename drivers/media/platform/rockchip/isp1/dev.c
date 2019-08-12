// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip isp1 driver
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-mipi-dphy.h>
#include <media/v4l2-mc.h>

#include "common.h"
#include "regs.h"

struct isp_match_data {
	const char * const *clks;
	unsigned int size;
};

/**************************** pipeline operations******************************/

static int __isp_pipeline_prepare(struct rkisp1_pipeline *p,
				  struct media_entity *me)
{
	struct rkisp1_device *dev = container_of(p, struct rkisp1_device, pipe);
	struct v4l2_subdev *sd;
	unsigned int i;

	p->num_subdevs = 0;
	memset(p->subdevs, 0, sizeof(p->subdevs));

	while (1) {
		struct media_pad *pad = NULL;

		/* Find remote source pad */
		for (i = 0; i < me->num_pads; i++) {
			struct media_pad *spad = &me->pads[i];

			if (!(spad->flags & MEDIA_PAD_FL_SINK))
				continue;
			pad = media_entity_remote_pad(spad);
			if (pad)
				break;
		}

		if (!pad)
			break;

		sd = media_entity_to_v4l2_subdev(pad->entity);
		if (sd != &dev->isp_sdev.sd)
			p->subdevs[p->num_subdevs++] = sd;

		me = &sd->entity;
		if (me->num_pads == 1)
			break;
	}
	return 0;
}

static int rkisp1_pipeline_open(struct rkisp1_pipeline *p,
				struct media_entity *me,
				bool prepare)
{
	int ret;

	if (WARN_ON(!p || !me))
		return -EINVAL;
	if (atomic_inc_return(&p->power_cnt) > 1)
		return 0;

	/* go through media graphic and get subdevs */
	if (prepare)
		__isp_pipeline_prepare(p, me);

	if (!p->num_subdevs)
		return -EINVAL;

	ret = v4l2_pipeline_pm_use(me, 1);
	if (ret < 0)
		return ret;

	return 0;
}

static int rkisp1_pipeline_close(struct rkisp1_pipeline *p,
				 struct media_entity *me)
{
	if (atomic_dec_return(&p->power_cnt) > 0)
		return 0;

	return v4l2_pipeline_pm_use(me, 0);
}

/*
 * stream-on order: isp_subdev, mipi dphy, sensor
 * stream-off order: mipi dphy, sensor, isp_subdev
 */
static int rkisp1_pipeline_set_stream(struct rkisp1_pipeline *p, bool on)
{
	struct rkisp1_device *dev = container_of(p, struct rkisp1_device, pipe);
	int i, ret;

	if ((on && atomic_inc_return(&p->stream_cnt) > 1) ||
	    (!on && atomic_dec_return(&p->stream_cnt) > 0))
		return 0;

	if (on) {
		ret = v4l2_subdev_call(&dev->isp_sdev.sd, video, s_stream,
				       true);
		if (ret && ret != -ENOIOCTLCMD && ret != -ENODEV) {
			dev_err(dev->dev,
				"s_stream failed on subdevice %s (%d)\n",
				dev->isp_sdev.sd.name,
				ret);
			atomic_dec(&p->stream_cnt);
			return ret;
		}
	}

	/* phy -> sensor */
	for (i = 0; i < p->num_subdevs; ++i) {
		ret = v4l2_subdev_call(p->subdevs[i], video, s_stream, on);
		if (on && ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
			goto err_stream_off;
	}

	if (!on)
		v4l2_subdev_call(&dev->isp_sdev.sd, video, s_stream, false);

	return 0;

err_stream_off:
	for (--i; i >= 0; --i)
		v4l2_subdev_call(p->subdevs[i], video, s_stream, false);
	v4l2_subdev_call(&dev->isp_sdev.sd, video, s_stream, false);
	atomic_dec(&p->stream_cnt);
	return ret;
}

/***************************** media controller *******************************/
/* See http://opensource.rock-chips.com/wiki_Rockchip-isp1 for Topology */

static int rkisp1_create_links(struct rkisp1_device *dev)
{
	struct media_entity *source, *sink;
	unsigned int flags, source_pad;
	struct v4l2_subdev *sd;
	int ret;

	/* sensor links */
	list_for_each_entry(sd, &dev->v4l2_dev.subdevs, list) {
		if (sd == &dev->isp_sdev.sd)
			continue;

		ret = media_entity_get_fwnode_pad(&sd->entity, sd->fwnode,
						  MEDIA_PAD_FL_SOURCE);
		if (ret < 0) {
			dev_err(sd->dev, "failed to find src pad for %s\n",
				sd->name);
			return ret;
		}
		source_pad = ret;

		ret = media_create_pad_link(
				&sd->entity, source_pad,
				&dev->isp_sdev.sd.entity,
				RKISP1_ISP_PAD_SINK,
				list_is_first(&sd->list,
					      &dev->v4l2_dev.subdevs) ?
				MEDIA_LNK_FL_ENABLED : 0);
		if (ret < 0)
			return ret;
	}

	/* params links */
	source = &dev->params_vdev.vnode.vdev.entity;
	sink = &dev->isp_sdev.sd.entity;
	flags = MEDIA_LNK_FL_ENABLED;
	ret = media_create_pad_link(source, 0, sink,
				    RKISP1_ISP_PAD_SINK_PARAMS, flags);
	if (ret < 0)
		return ret;

	/* create isp internal links */
	/* SP links */
	source = &dev->isp_sdev.sd.entity;
	sink = &dev->stream[RKISP1_STREAM_SP].vnode.vdev.entity;
	ret = media_create_pad_link(source, RKISP1_ISP_PAD_SOURCE_PATH,
				    sink, 0, flags);
	if (ret < 0)
		return ret;

	/* MP links */
	source = &dev->isp_sdev.sd.entity;
	sink = &dev->stream[RKISP1_STREAM_MP].vnode.vdev.entity;
	ret = media_create_pad_link(source, RKISP1_ISP_PAD_SOURCE_PATH,
				    sink, 0, flags);
	if (ret < 0)
		return ret;

	/* 3A stats links */
	source = &dev->isp_sdev.sd.entity;
	sink = &dev->stats_vdev.vnode.vdev.entity;
	return media_create_pad_link(source, RKISP1_ISP_PAD_SOURCE_STATS,
				     sink, 0, flags);
}

static int subdev_notifier_bound(struct v4l2_async_notifier *notifier,
                                struct v4l2_subdev *sd,
                                struct v4l2_async_subdev *asd)
{
	struct rkisp1_device *isp_dev = container_of(notifier,
						     struct rkisp1_device,
						     notifier);
	struct sensor_async_subdev *s_asd = container_of(asd,
					struct sensor_async_subdev, asd);

	s_asd->sd = sd;
	s_asd->dphy = devm_phy_get(isp_dev->dev, "dphy");
	if (IS_ERR(s_asd->dphy)) {
		if (PTR_ERR(s_asd->dphy) != -EPROBE_DEFER)
			dev_err(isp_dev->dev, "Couldn't get the MIPI D-PHY\n");
		return PTR_ERR(s_asd->dphy);
	}

	phy_init(s_asd->dphy);

	return 0;
}

static void subdev_notifier_unbind(struct v4l2_async_notifier *notifier,
				   struct v4l2_subdev *sd,
				   struct v4l2_async_subdev *asd)
{
	struct sensor_async_subdev *s_asd = container_of(asd,
					struct sensor_async_subdev, asd);

	phy_exit(s_asd->dphy);
}

static int subdev_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct rkisp1_device *dev = container_of(notifier, struct rkisp1_device,
						 notifier);
	int ret;

	mutex_lock(&dev->media_dev.graph_mutex);
	ret = rkisp1_create_links(dev);
	if (ret < 0)
		goto unlock;
	ret = v4l2_device_register_subdev_nodes(&dev->v4l2_dev);
	if (ret < 0)
		goto unlock;

	dev_info(dev->dev, "Async subdev notifier completed\n");

unlock:
	mutex_unlock(&dev->media_dev.graph_mutex);
	return ret;
}

static int rkisp1_fwnode_parse(struct device *dev,
			       struct v4l2_fwnode_endpoint *vep,
			       struct v4l2_async_subdev *asd)
{
	struct sensor_async_subdev *s_asd =
			container_of(asd, struct sensor_async_subdev, asd);

	if (vep->bus_type != V4L2_MBUS_CSI2_DPHY) {
		dev_err(dev, "Only CSI2 bus type is currently supported\n");
		return -EINVAL;
	}

	if (vep->base.port != 0) {
		dev_err(dev, "The ISP has only port 0\n");
		return -EINVAL;
	}

	s_asd->mbus.type = vep->bus_type;
	s_asd->mbus.flags = vep->bus.mipi_csi2.flags;
	s_asd->lanes = vep->bus.mipi_csi2.num_data_lanes;

	switch (vep->bus.mipi_csi2.num_data_lanes) {
	case 1:
		s_asd->mbus.flags |= V4L2_MBUS_CSI2_1_LANE;
		break;
	case 2:
		s_asd->mbus.flags |= V4L2_MBUS_CSI2_2_LANE;
		break;
	case 3:
		s_asd->mbus.flags |= V4L2_MBUS_CSI2_3_LANE;
		break;
	case 4:
		s_asd->mbus.flags |= V4L2_MBUS_CSI2_4_LANE;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_async_notifier_operations subdev_notifier_ops = {
	.bound = subdev_notifier_bound,
	.unbind = subdev_notifier_unbind,
	.complete = subdev_notifier_complete,
};

static int isp_subdev_notifier(struct rkisp1_device *isp_dev)
{
	struct v4l2_async_notifier *ntf = &isp_dev->notifier;
	struct device *dev = isp_dev->dev;
	int ret;

	v4l2_async_notifier_init(ntf);

	ret = v4l2_async_notifier_parse_fwnode_endpoints_by_port(
		dev, ntf, sizeof(struct sensor_async_subdev), 0,
		rkisp1_fwnode_parse);
	if (ret < 0)
		return ret;

	if (list_empty(&ntf->asd_list))
		return -ENODEV;	/* no endpoint */

	ntf->ops = &subdev_notifier_ops;

	return v4l2_async_notifier_register(&isp_dev->v4l2_dev, ntf);
}

/***************************** platform device *******************************/

static int rkisp1_register_platform_subdevs(struct rkisp1_device *dev)
{
	int ret;

	ret = rkisp1_register_isp_subdev(dev, &dev->v4l2_dev);
	if (ret < 0)
		return ret;

	ret = rkisp1_register_stream_vdevs(dev);
	if (ret < 0)
		goto err_unreg_isp_subdev;

	ret = rkisp1_register_stats_vdev(&dev->stats_vdev, &dev->v4l2_dev, dev);
	if (ret < 0)
		goto err_unreg_stream_vdev;

	ret = rkisp1_register_params_vdev(&dev->params_vdev, &dev->v4l2_dev,
					  dev);
	if (ret < 0)
		goto err_unreg_stats_vdev;

	ret = isp_subdev_notifier(dev);
	if (ret < 0) {
		dev_err(dev->dev,
			"Failed to register subdev notifier(%d)\n", ret);
		goto err_unreg_params_vdev;
	}

	return 0;
err_unreg_params_vdev:
	rkisp1_unregister_params_vdev(&dev->params_vdev);
err_unreg_stats_vdev:
	rkisp1_unregister_stats_vdev(&dev->stats_vdev);
err_unreg_stream_vdev:
	rkisp1_unregister_stream_vdevs(dev);
err_unreg_isp_subdev:
	rkisp1_unregister_isp_subdev(dev);
	return ret;
}

static const char * const rk3399_isp_clks[] = {
	"clk_isp",
	"aclk_isp",
	"hclk_isp",
	"aclk_isp_wrap",
	"hclk_isp_wrap",
};

static const char * const rk3288_isp_clks[] = {
	"clk_isp",
	"aclk_isp",
	"hclk_isp",
	"pclk_isp_in",
	"sclk_isp_jpe",
};

static const struct isp_match_data rk3288_isp_clk_data = {
	.clks = rk3288_isp_clks,
	.size = ARRAY_SIZE(rk3288_isp_clks),
};

static const struct isp_match_data rk3399_isp_clk_data = {
	.clks = rk3399_isp_clks,
	.size = ARRAY_SIZE(rk3399_isp_clks),
};

static const struct of_device_id rkisp1_plat_of_match[] = {
	{
		.compatible = "rockchip,rk3288-cif-isp",
		.data = &rk3288_isp_clk_data,
	}, {
		.compatible = "rockchip,rk3399-cif-isp",
		.data = &rk3399_isp_clk_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, rkisp1_plat_of_match);

static irqreturn_t rkisp1_irq_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp1_device *rkisp1_dev = dev_get_drvdata(dev);
	unsigned int mis_val;

	mis_val = readl(rkisp1_dev->base_addr + CIF_ISP_MIS);
	if (mis_val)
		rkisp1_isp_isr(mis_val, rkisp1_dev);

	mis_val = readl(rkisp1_dev->base_addr + CIF_MIPI_MIS);
	if (mis_val)
		rkisp1_mipi_isr(mis_val, rkisp1_dev);

	mis_val = readl(rkisp1_dev->base_addr + CIF_MI_MIS);
	if (mis_val)
		rkisp1_mi_isr(mis_val, rkisp1_dev);

	return IRQ_HANDLED;
}

static int rkisp1_plat_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	const struct isp_match_data *clk_data;
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct rkisp1_device *isp_dev;
	struct v4l2_device *v4l2_dev;
	unsigned int i;
	int ret, irq;

	match = of_match_node(rkisp1_plat_of_match, node);
	isp_dev = devm_kzalloc(dev, sizeof(*isp_dev), GFP_KERNEL);
	if (!isp_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, isp_dev);
	isp_dev->dev = dev;

	isp_dev->base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(isp_dev->base_addr))
		return PTR_ERR(isp_dev->base_addr);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, rkisp1_irq_handler, IRQF_SHARED,
			       dev_driver_string(dev), dev);
	if (ret < 0) {
		dev_err(dev, "request irq failed: %d\n", ret);
		return ret;
	}

	isp_dev->irq = irq;
	clk_data = match->data;

	for (i = 0; i < clk_data->size; i++)
		isp_dev->clks[i].id = clk_data->clks[i];
	ret = devm_clk_bulk_get(dev, clk_data->size, isp_dev->clks);
	if (ret)
		return ret;
	isp_dev->clk_size = clk_data->size;

	atomic_set(&isp_dev->pipe.power_cnt, 0);
	atomic_set(&isp_dev->pipe.stream_cnt, 0);
	isp_dev->pipe.open = rkisp1_pipeline_open;
	isp_dev->pipe.close = rkisp1_pipeline_close;
	isp_dev->pipe.set_stream = rkisp1_pipeline_set_stream;

	rkisp1_stream_init(isp_dev, RKISP1_STREAM_SP);
	rkisp1_stream_init(isp_dev, RKISP1_STREAM_MP);

	strscpy(isp_dev->media_dev.model, "rkisp1",
		sizeof(isp_dev->media_dev.model));
	isp_dev->media_dev.dev = &pdev->dev;
	strscpy(isp_dev->media_dev.bus_info,
		"platform: " DRIVER_NAME, sizeof(isp_dev->media_dev.bus_info));
	media_device_init(&isp_dev->media_dev);

	v4l2_dev = &isp_dev->v4l2_dev;
	v4l2_dev->mdev = &isp_dev->media_dev;
	strscpy(v4l2_dev->name, "rkisp1", sizeof(v4l2_dev->name));

	ret = v4l2_device_register(isp_dev->dev, &isp_dev->v4l2_dev);
	if (ret < 0)
		return ret;

	ret = media_device_register(&isp_dev->media_dev);
	if (ret < 0) {
		dev_err(dev, "Failed to register media device: %d\n", ret);
		goto err_unreg_v4l2_dev;
	}

	/* create & register platefom subdev (from of_node) */
	ret = rkisp1_register_platform_subdevs(isp_dev);
	if (ret < 0)
		goto err_unreg_media_dev;

	pm_runtime_enable(&pdev->dev);

	return 0;

err_unreg_media_dev:
	media_device_unregister(&isp_dev->media_dev);
err_unreg_v4l2_dev:
	v4l2_device_unregister(&isp_dev->v4l2_dev);
	return ret;
}

static int rkisp1_plat_remove(struct platform_device *pdev)
{
	struct rkisp1_device *isp_dev = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	media_device_unregister(&isp_dev->media_dev);
	v4l2_async_notifier_unregister(&isp_dev->notifier);
	v4l2_async_notifier_cleanup(&isp_dev->notifier);
	v4l2_device_unregister(&isp_dev->v4l2_dev);
	rkisp1_unregister_params_vdev(&isp_dev->params_vdev);
	rkisp1_unregister_stats_vdev(&isp_dev->stats_vdev);
	rkisp1_unregister_stream_vdevs(isp_dev);
	rkisp1_unregister_isp_subdev(isp_dev);

	return 0;
}

static int __maybe_unused rkisp1_runtime_suspend(struct device *dev)
{
	struct rkisp1_device *isp_dev = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(isp_dev->clk_size, isp_dev->clks);
	return pinctrl_pm_select_sleep_state(dev);
}

static int __maybe_unused rkisp1_runtime_resume(struct device *dev)
{
	struct rkisp1_device *isp_dev = dev_get_drvdata(dev);
	int ret;

	ret = pinctrl_pm_select_default_state(dev);
	if (ret < 0)
		return ret;
	ret = clk_bulk_prepare_enable(isp_dev->clk_size, isp_dev->clks);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct dev_pm_ops rkisp1_plat_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(rkisp1_runtime_suspend, rkisp1_runtime_resume, NULL)
};

static struct platform_driver rkisp1_plat_drv = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(rkisp1_plat_of_match),
		.pm = &rkisp1_plat_pm_ops,
	},
	.probe = rkisp1_plat_probe,
	.remove = rkisp1_plat_remove,
};

module_platform_driver(rkisp1_plat_drv);
MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("Rockchip ISP1 platform driver");
MODULE_LICENSE("Dual BSD/GPL");
