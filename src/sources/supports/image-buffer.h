////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing) 
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////
#ifndef __IMAGE_BUFFER_H__
#define __IMAGE_BUFFER_H__

#include "win-types.h"

struct xi_heap;
struct xi_mem_desc;

struct xi_image_buffer {
	struct xi_heap     *m_pHeap;
	u32					m_cx;
	u32					m_cy;
	DWORD				m_cbStride;
    struct xi_mem_desc *m_pMemDesc;
};

bool xi_image_buffer_create(struct xi_image_buffer *imgbuf, struct xi_heap *heap,
        u32 cx, u32 cy, u32 stride);

void xi_image_buffer_destroy(struct xi_image_buffer *imgbuf);

static inline bool xi_image_buffer_isvalid(struct xi_image_buffer *imgbuf)
{
    return (imgbuf->m_pMemDesc != NULL);
}

u32 xi_image_buffer_get_address(struct xi_image_buffer *imgbuf);

static inline u32 xi_image_buffer_get_width(struct xi_image_buffer *imgbuf)
{
    return imgbuf->m_cx;
}

static inline u32 xi_image_buffer_get_height(struct xi_image_buffer *imgbuf)
{
    return imgbuf->m_cy;
}

static inline u32 xi_image_buffer_get_stride(struct xi_image_buffer *imgbuf)
{
    return imgbuf->m_cbStride;
}

#endif /* __IMAGE_BUFFER_H__ */
