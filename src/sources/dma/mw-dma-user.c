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
#include <linux/version.h>

#include "ospi/ospi.h"

#include "mw-dma-mem-priv.h"

struct priv_dma_desc {
    struct mw_dma_desc          dma_desc;

    int                         direction;
    int                         offset;
    size_t                      size;
    struct page                 **pages;
    int                         nr_pages;
    struct device               *dev;

    struct scatterlist          *sglist;
};

static int _dma_init_user(struct priv_dma_desc *dma,
                          int direction, unsigned long vaddr,
                          unsigned long size)
{
    unsigned long first, last;
    int err = 0;
    int write = 0; /* if the device write */

    dma->direction = direction;
    switch (dma->direction) {
    case MW_DMA_FROM_DEVICE:
        write = 1;
        break;
    case MW_DMA_TO_DEVICE:
        write = 0;
        break;
    case MW_DMA_BIDIRECTIONAL:
        write = 1;
        break;
    default:
        BUG();
    }

    first = (vaddr          & PAGE_MASK) >> PAGE_SHIFT;
    last  = ((vaddr+size-1) & PAGE_MASK) >> PAGE_SHIFT;
    dma->offset = vaddr & ~PAGE_MASK;
    dma->size = size;
    dma->nr_pages = last-first+1;
    dma->pages = kmalloc(dma->nr_pages * sizeof(struct page *), GFP_KERNEL);
    if (NULL == dma->pages)
        return -ENOMEM;

    xi_debug(20, "init user [0x%lx+0x%lx => %d pages]\n",
            vaddr, size, dma->nr_pages);

    err= get_user_pages_fast(vaddr & PAGE_MASK, dma->nr_pages, write, dma->pages);

    if (err != dma->nr_pages) {
        xi_debug(1, "get_user_pages got/requested: %d/%d]\n",
                (err >= 0) ? err : 0, dma->nr_pages);
        while (--err >= 0)
            put_page(dma->pages[err]);
        kfree(dma->pages);
        return err < 0 ? err : -EINVAL;
    }
    return 0;
}

static struct scatterlist *_pages_to_sg(
        struct page **pages,
        int nr_pages, int offset, size_t size
        )
{
    struct scatterlist *sglist;
    int i;

    if (NULL == pages[0])
        return NULL;

    sglist = vmalloc(nr_pages * sizeof(*sglist));
    if (NULL == sglist)
        return NULL;
    sg_init_table(sglist, nr_pages);

    sg_set_page(&sglist[0], pages[0], PAGE_SIZE - offset, offset);
    size -= PAGE_SIZE - offset;
    for (i = 1; i < nr_pages; i++) {
        if (NULL == pages[i])
            goto nopage;
        sg_set_page(&sglist[i], pages[i], min_t(size_t, PAGE_SIZE, size), 0);
        size -= min_t(size_t, PAGE_SIZE, size);
    }
    return sglist;

nopage:
    xi_debug(0, "sgl: oops - no page\n");
    vfree(sglist);
    return NULL;
}

static void _dma_uninit(struct priv_dma_desc *dma)
{
    int i;

    if (dma->pages) {
        for (i = 0; i < dma->nr_pages; i++)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0)
            put_page(dma->pages[i]);
#else
            page_cache_release(dma->pages[i]);
#endif
        kfree(dma->pages);
        dma->pages = NULL;
    }

    dma->direction = MW_DMA_NONE;
}

static int _dma_sg_create(struct mw_dma_desc **dma_desc,
                          unsigned long addr, size_t size,
                          int direction,
                          void *private_data)
{
    struct priv_dma_desc *priv_dma = NULL;
    struct scatterlist *sglist = NULL;
    struct device *dev = (struct device *)private_data;
    int ret = 0;
    int sglen;
    int i;

    if (size <= 0 || dma_desc == NULL)
        return -EINVAL;

    priv_dma = kzalloc(sizeof(*priv_dma), GFP_KERNEL);
    if (priv_dma == NULL)
        return -ENOMEM;

    priv_dma->dev = dev;

    ret = _dma_init_user(priv_dma, direction, addr, size);
    if (ret != 0)
        goto init_err;

    sglist = _pages_to_sg(priv_dma->pages, priv_dma->nr_pages, priv_dma->offset, size);
    if (sglist == NULL) {
        ret = -ENOMEM;
        goto sglist_err;
    }

    sglen = dma_map_sg(priv_dma->dev, sglist,
                       priv_dma->nr_pages,
                       mw_dma_direction_to_os(priv_dma->direction));
    if (sglen <= 0) {
        printk(KERN_ERR "%s: dma_map_sg failed sglen=%d\n", __func__, sglen);
        ret = -ENOMEM;
        goto map_err;
    }

    priv_dma->dma_desc.mwsg_list = os_malloc(sizeof(*(priv_dma->dma_desc.mwsg_list)) * sglen);
    if (priv_dma->dma_desc.mwsg_list == NULL)
        goto mwsg_err;

    for (i = 0; i < sglen; i++) {
        mw_sg_dma_address(&priv_dma->dma_desc.mwsg_list[i]) = sg_dma_address(&sglist[i]);
        mw_sg_dma_len(&priv_dma->dma_desc.mwsg_list[i]) = sg_dma_len(&sglist[i]);
    }
    priv_dma->dma_desc.sglen = sglen;

    priv_dma->sglist = sglist;

    *dma_desc = &priv_dma->dma_desc;

    return 0;

mwsg_err:
    dma_unmap_sg(priv_dma->dev, sglist,
                 sglen, mw_dma_direction_to_os(priv_dma->direction));
map_err:
    vfree(sglist);
sglist_err:
    _dma_uninit(priv_dma);
init_err:
    kfree(priv_dma);
    return ret;
}

static void _dma_sg_destroy(struct mw_dma_desc *dma_desc)
{
    struct priv_dma_desc *priv_dma;

    if (dma_desc == NULL || !dma_desc->sglen)
        return;

    priv_dma = (struct priv_dma_desc *)container_of(dma_desc, struct priv_dma_desc, dma_desc);

    dma_unmap_sg(priv_dma->dev, priv_dma->sglist,
                 priv_dma->nr_pages, mw_dma_direction_to_os(priv_dma->direction));

    vfree(priv_dma->sglist);
    priv_dma->sglist = NULL;

    os_free(dma_desc->mwsg_list);
    dma_desc->mwsg_list = NULL;
    dma_desc->sglen = 0;

    _dma_uninit(priv_dma);

    kfree(priv_dma);

    return;
}

static int _dma_sg_sync_for_cpu(struct mw_dma_desc *dma_desc)
{
    struct priv_dma_desc *priv_dma =
            (struct priv_dma_desc *)container_of(dma_desc, struct priv_dma_desc, dma_desc);

    dma_sync_sg_for_cpu(priv_dma->dev, priv_dma->sglist,
            priv_dma->nr_pages, priv_dma->direction);

    return 0;
}

static int _dma_sg_sync_for_device(struct mw_dma_desc *dma_desc)
{
    struct priv_dma_desc *priv_dma =
            (struct priv_dma_desc *)container_of(dma_desc, struct priv_dma_desc, dma_desc);

    dma_sync_sg_for_device(priv_dma->dev, priv_dma->sglist,
            priv_dma->nr_pages, priv_dma->direction);

    return 0;
}

struct mw_dma_memory_client user_dma_client = {
    .mem_type               = MWCAP_VIDEO_MEMORY_TYPE_USER,
    .create_dma_desc        = _dma_sg_create,
    .destroy_dma_desc       = _dma_sg_destroy,
    .sync_for_cpu           = _dma_sg_sync_for_cpu,
    .sync_for_device        = _dma_sg_sync_for_device,
};
