////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing) 
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////

#ifndef __XI_TIMER_H__
#define __XI_TIMER_H__

#include "ospi/ospi.h"

typedef struct _xi_timer {
    struct list_head      user_node;

	int			          m_nIndex;
    long long             m_llExpireTime;

    os_event_t            event;
    bool                  need_free;
} xi_timer;

#endif /* __XI_TIMER_H__ */

