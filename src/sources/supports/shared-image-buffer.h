////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////
#ifndef __SHARED_IMAGE_BUFFER_H__
#define __SHARED_IMAGE_BUFFER_H__

#include "ospi/ospi.h"

#include "image-buffer.h"

struct shared_image_buf {
    struct list_head        list_node;
    struct xi_image_buffer  image_buf;
    long                    ref_count;
};

struct shared_image_user_node {
    struct list_head        user_node;
    struct shared_image_buf *shared_buf;
};

struct shared_image_buf_manager {
    os_spinlock_t           m_lock;
    struct xi_heap          *m_pheap;
    struct list_head        m_buf_list;
};

int shared_image_buf_manager_init(struct shared_image_buf_manager *manager, struct xi_heap *pheap);

void shared_image_buf_manager_deinit(struct shared_image_buf_manager *manager);

struct shared_image_buf *shared_image_buf_create(struct shared_image_buf_manager *manager,
        int cx, int cy);

bool shared_image_buf_open(struct shared_image_buf_manager *manager, struct shared_image_buf *pbuf,
        long *ref_count);

bool shared_image_buf_close(struct shared_image_buf_manager *manager, struct shared_image_buf *pbuf,
        long *ref_count);

bool shared_image_buf_is_valid(struct shared_image_buf_manager *manager,
        struct shared_image_buf *pbuf);

#endif /* __SHARED_IMAGE_BUFFER_H__ */
