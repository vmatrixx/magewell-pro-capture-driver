////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////

#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/pagemap.h>

#include "mw-dma-mem-priv.h"

#include "ospi/ospi.h"

struct priv_dma_desc {
    struct mw_dma_desc          dma_desc;

    dma_addr_t                  addr;
    int                         direction;
    size_t                      size;
};

static void _dma_uninit(struct priv_dma_desc *dma)
{
    if (dma->addr) {
        dma->addr = 0;
    }

    dma->direction = MW_DMA_NONE;
}

static int _dma_sg_create(struct mw_dma_desc **dma_desc,
                          unsigned long addr, size_t size,
                          int direction,
                          void *private_data)
{
    struct priv_dma_desc *priv_dma = NULL;
    mw_scatterlist_t *mwsg_list = NULL;
    int ret = 0;
    int sglen;

    if (size <= 0 || dma_desc == NULL)
        return -EINVAL;

    priv_dma = kzalloc(sizeof(*priv_dma), GFP_KERNEL);
    if (priv_dma == NULL)
        return -ENOMEM;

    priv_dma->direction = direction;
    priv_dma->addr = (dma_addr_t)addr;
    priv_dma->size = size;

    mwsg_list = kzalloc(sizeof(*mwsg_list), GFP_KERNEL);
    if (mwsg_list == NULL) {
        ret = -ENOMEM;
        goto sglist_err;
    }

    sglen = 1;
    mw_sg_dma_address(&mwsg_list[0]) = priv_dma->addr;
    mw_sg_dma_len(&mwsg_list[0]) = priv_dma->size;

    priv_dma->dma_desc.mwsg_list = mwsg_list;
    priv_dma->dma_desc.sglen = sglen;

    *dma_desc = &priv_dma->dma_desc;

    return 0;

sglist_err:
    _dma_uninit(priv_dma);
    kfree(priv_dma);
    return ret;
}

static void _dma_sg_destroy(struct mw_dma_desc *dma_desc)
{
    struct priv_dma_desc *priv_dma;

    if (dma_desc == NULL || !dma_desc->sglen)
        return;

    priv_dma = (struct priv_dma_desc *)container_of(dma_desc, struct priv_dma_desc, dma_desc);

    kfree(dma_desc->mwsg_list);
    dma_desc->mwsg_list = NULL;
    dma_desc->sglen = 0;

    _dma_uninit(priv_dma);

    kfree(priv_dma);

    return;
}

static int _dma_sg_sync_for_cpu(struct mw_dma_desc *dma_desc)
{
    return 0;
}

static int _dma_sg_sync_for_device(struct mw_dma_desc *dma_desc)
{
    return 0;
}

struct mw_dma_memory_client phy_dma_client = {
    .mem_type               = MWCAP_VIDEO_MEMORY_TYPE_PHYSICAL,
    .create_dma_desc        = _dma_sg_create,
    .destroy_dma_desc       = _dma_sg_destroy,
    .sync_for_cpu           = _dma_sg_sync_for_cpu,
    .sync_for_device        = _dma_sg_sync_for_device,
};
