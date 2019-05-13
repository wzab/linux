/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2019 Collabora ltd. */

#ifndef __PANFROST_DEVFREQ_H__
#define __PANFROST_DEVFREQ_H__

#if defined(CONFIG_PM_DEVFREQ)
int panfrost_devfreq_init(struct panfrost_device *pfdev);
void panfrost_devfreq_resume(struct panfrost_device *pfdev);
void panfrost_devfreq_suspend(struct panfrost_device *pfdev);
void panfrost_devfreq_record_transition(struct panfrost_device *pfdev, int slot);
#else
static inline int panfrost_devfreq_init(struct panfrost_device *pfdev)
{
	return 0;
}

static inline void panfrost_devfreq_resume(struct panfrost_device *pfdev)
{}

static inline void panfrost_devfreq_suspend(struct panfrost_device *pfdev)
{}

static inline void panfrost_devfreq_record_transition(struct panfrost_device *pfdev, int slot)
{}
#endif

#endif /* __PANFROST_DEVFREQ_H__ */
