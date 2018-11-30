/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * vimc-core.h Virtual Media Controller Driver
 *
 * Copyright (C) 2018 Helen Koike <helen.koike@collabora.com>
 */

#ifndef _VIMC_CORE_H_
#define _VIMC_CORE_H_

#define VIMC_CORE_PDEV_NAME "vimc-core"

int vimc_core_comp_bind(struct device *master);

void vimc_core_comp_unbind(struct device *master);

#endif
