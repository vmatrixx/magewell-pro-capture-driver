////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////

#ifndef __MW_EVENT_DEV_H__
#define __MW_EVENT_DEV_H__

#include "ospi/ospi.h"

int mw_event_dev_create(void);

void mw_event_dev_destroy(void);

bool mw_event_is_event_valid(os_event_t event);

#endif /* __MW_EVENT_DEV_H__ */
