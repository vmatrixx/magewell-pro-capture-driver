////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2018 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////
#ifndef __XI_V4L2_H__
#define __XI_V4L2_H__

#include <media/v4l2-device.h>

#include "mw-stream.h"

struct mw_v4l2_channel {
    void                        *driver; /* low level card driver */

    /* video device */
    struct video_device *       vfd;
    struct device *             pci_dev;

    struct mw_device            mw_dev;
};

struct xi_v4l2_dev {
    struct v4l2_device          v4l2_dev;

    void                        *driver; /* low level card driver */
    void                        *parent_dev;

    char                        driver_name[32];

    struct mw_v4l2_channel      *mw_vchs;
    int                         mw_vchs_count;
};

int xi_v4l2_init(struct xi_v4l2_dev *dev, void *driver, void *parent_dev);

int xi_v4l2_release(struct xi_v4l2_dev *dev);

#ifdef CONFIG_PM
int xi_v4l2_suspend(struct xi_v4l2_dev *dev);

int xi_v4l2_resume(struct xi_v4l2_dev *dev);
#endif

#endif /* __XI_V4L2_H__ */
