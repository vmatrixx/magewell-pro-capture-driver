////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////
#ifndef __MW_EVENT_IOCTL_PRIV__
#define __MW_EVENT_IOCTL_PRIV__

#include "mw-event-ioctl.h"
#include "ospi/ospi.h"

struct mw_event_io {
    os_spinlock_t               event_lock;
    struct list_head            event_list;
};

int mw_event_ioctl_init(struct mw_event_io *mwev);

void mw_event_ioctl_deinit(struct mw_event_io *mwev);

int mw_event_ioctl(struct mw_event_io *mwev, os_iocmd_t cmd, void *arg);

bool mw_event_is_event_valid_nolock(struct mw_event_io *mwev, os_event_t event);

#endif /* __MW_EVENT_IOCTL_PRIV__ */
