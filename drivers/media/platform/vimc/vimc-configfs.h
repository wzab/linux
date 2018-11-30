/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * vimc-configfs.h Virtual Media Controller Driver
 *
 * Copyright (C) 2018 Helen Koike <helen.koike@collabora.com>
 */

#ifndef _VIMC_CONFIGFS_H_
#define _VIMC_CONFIGFS_H_

#include <linux/configfs.h>

#define VIMC_CFS_SRC_PAD_NAME(n) "pad:source:" #n
#define VIMC_CFS_SINK_PAD_NAME(n) "pad:sink:" #n

struct vimc_cfs_drv {
	const char *name;
	void (*const configfs_cb)(struct config_group *group);
	struct list_head list;
};

int vimc_cfs_subsys_register(void);

void vimc_cfs_subsys_unregister(void);

void vimc_cfs_drv_register(struct vimc_cfs_drv *c_drv);

void vimc_cfs_drv_unregister(struct vimc_cfs_drv *c_drv);

#endif
