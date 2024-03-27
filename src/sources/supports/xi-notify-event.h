////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing) 
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////

#ifndef __XI_NOTIFY_EVENT_H__
#define __XI_NOTIFY_EVENT_H__

#include "ospi/ospi.h"

typedef struct _xi_notify_event {
    struct list_head      user_node;
    struct list_head      list_node;

    u64                   enable_bits;
    u64                   status_bits;
    os_event_t            event;
    bool                  need_free;
} xi_notify_event;

#endif /* __XI_NOTIFY_EVENT_H__ */

