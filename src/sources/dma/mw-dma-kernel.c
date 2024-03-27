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

    /* vmalloc */
    void                        *vaddr;

    /* kmalloc */
    void                        *kaddr;


    int                         nr_pages;
    int                         direction;
    size_t                      size;
    struct device               *dev;

    struct scatterlist          *sglist;
};

static struct scatterlist *_vmalloc_to_sg(struct priv_dma_desc *dma)
{
    struct scatterlist *sglist;
    struct page *pg;
    int i;
    unsigned long first, last;
    unsigned long data, offset;
    size_t size;
    int nr_pages;

    data = (unsigned long)dma->vaddr;
    size = dma->size;

    first = (data          & PAGE_MASK) >> PAGE_SHIFT;
    last  = ((data+size-1) & PAGE_MASK) >> PAGE_SHIFT;
    offset = data & ~PAGE_MASK;
    dma->nr_pages = nr_pages = last-first+1;

    sglist = os_malloc(nr_pages * sizeof(*sglist));
    if (NULL == sglist)
        return NULL;
    memset(sglist, 0, nr_pages * sizeof(*sglist));
    sg_init_table(sglist, nr_pages);

    pg = vmalloc_to_page((void *)(first << PAGE_SHIFT));
    sg_set_page(&sglist[0], pg, PAGE_SIZE-offset, offset);
    size -= PAGE_SIZE-offset;
    data += PAGE_SIZE-offset;

    for (i = 1; i < nr_pages; i++, data += PAGE_SIZE) {
        pg = vmalloc_to_page((void *)data);
        if (NULL == pg)
            goto err;
        sg_set_page(&sglist[i], pg, min_t(size_t, PAGE_SIZE, size), 0);
        size -= min_t(size_t, PAGE_SIZE, size);
    }
    return sglist;

err:
    os_free(sglist);
    return NULL;
}

static void _dma_uninit(struct priv_dma_desc *dma)
{
    if (dma->vaddr) {
        dma->vaddr = NULL;
    }
    if (dma->kaddr) {
        dma->kaddr = NULL;
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

    priv_dma = os_zalloc(sizeof(*priv_dma));
    if (priv_dma == NULL)
        return -ENOMEM;

    priv_dma->dev = dev;

    priv_dma->direction = direction;
    priv_dma->size = size;

    if (os_mem_get_mem_type((void *)addr) == OS_LINUX_MEM_TYPE_KMALLOC) {
        priv_dma->kaddr = (void *)addr;
        sglist = os_zalloc(sizeof(*sglist));
        if (sglist == NULL) {
            ret = -ENOMEM;
            goto sglist_err;
        }

        priv_dma->nr_pages = 1;

        sg_init_one(&sglist[0], (void *)addr, size);
    } else {
        priv_dma->vaddr = (void *)addr;

        sglist = _vmalloc_to_sg(priv_dma);
        if (sglist == NULL) {
            ret = -ENOMEM;
            goto sglist_err;
        }
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
    os_free(sglist);
sglist_err:
    _dma_uninit(priv_dma);
    os_free(priv_dma);
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

    os_free(priv_dma->sglist);
    priv_dma->sglist = NULL;

    os_free(dma_desc->mwsg_list);
    dma_desc->mwsg_list = NULL;
    dma_desc->sglen = 0;

    _dma_uninit(priv_dma);

    os_free(priv_dma);

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

    dma_sync_sg_for_cpu(priv_dma->dev, priv_dma->sglist,
                        priv_dma->nr_pages, priv_dma->direction);

    return 0;
}

struct mw_dma_memory_client kernel_dma_client = {
    .mem_type               = MWCAP_VIDEO_MEMORY_TYPE_KERNEL,
    .create_dma_desc        = _dma_sg_create,
    .destroy_dma_desc       = _dma_sg_destroy,
    .sync_for_cpu           = _dma_sg_sync_for_cpu,
    .sync_for_device        = _dma_sg_sync_for_device,
};
