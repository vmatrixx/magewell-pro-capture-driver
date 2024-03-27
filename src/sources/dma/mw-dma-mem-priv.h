////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////
#ifndef __MW_DMA_DESC_PRIV_H__
#define __MW_DMA_DESC_PRIV_H__

#include "mw-dma-mem.h"

#define MWCAP_VIDEO_MEMORY_TYPE_KERNEL     (1)

int mw_dma_memory_init(void);

void mw_dma_memory_deinit(void);

int mw_dma_memory_create_desc(struct mw_dma_desc **desc,
                              int mem_type,
                              unsigned long addr, size_t size,
                              int direction,
                              void *private_data);

int mw_dma_memory_destroy_desc(struct mw_dma_desc *desc);

static inline int mw_dma_memory_sync_for_cpu(struct mw_dma_desc *desc)
{
    return desc->client->sync_for_cpu(desc);
}

static inline int mw_dma_memory_sync_for_device(struct mw_dma_desc *desc)
{
    return desc->client->sync_for_device(desc);
}

int mw_dma_direction_to_os(int mw_direction);

extern struct mw_dma_memory_client kernel_dma_client;
extern struct mw_dma_memory_client user_dma_client;
extern struct mw_dma_memory_client phy_dma_client;

#endif /* __MW_DMA_DESC_PRIV_H__ */
