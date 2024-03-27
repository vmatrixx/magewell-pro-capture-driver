////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////
#include "ospi/ospi.h"

#include "mw-dma-mem-priv.h"

#include "mw-procapture-extension.h"

#include <linux/dma-mapping.h>

struct mw_dma_mem_client_internal {
    struct mw_dma_memory_client     *client;

    os_mutex_t                      mutex;
    int                             ref_count;
};

static struct mw_dma_mem_client_internal g_mw_dma_clients[MW_DMA_MEMORY_MAX_CLIENT];

int mw_dma_direction_to_os(int mw_direction)
{
    switch (mw_direction) {
    case MW_DMA_TO_DEVICE:
        return DMA_TO_DEVICE;
    case MW_DMA_FROM_DEVICE:
        return DMA_FROM_DEVICE;
    case MW_DMA_BIDIRECTIONAL:
        return DMA_BIDIRECTIONAL;
    default:
        return DMA_NONE;
    }
}

static int mw_dma_memory_check_mandatory(struct mw_dma_memory_client *client)
{
#define DMA_MEM_MANDATORY_FUNC(x) {\
    offsetof(struct mw_dma_memory_client, x), #x }

    static const struct {
        size_t offset;
        char  *name;
    } mandatory_table[] = {
        DMA_MEM_MANDATORY_FUNC(create_dma_desc),
        DMA_MEM_MANDATORY_FUNC(destroy_dma_desc),
        DMA_MEM_MANDATORY_FUNC(sync_for_cpu),
        DMA_MEM_MANDATORY_FUNC(sync_for_device)
    };
    int i;

    for (i = 0; i < ARRAY_SIZE(mandatory_table); ++i) {
        if (!*(void **) ((void *) client + mandatory_table[i].offset)) {
            os_print_warning("Dma memory %d is missing mandatory function %s\n",
                   client->mem_type, mandatory_table[i].name);
            return OS_RETURN_INVAL;
        }
    }

    return 0;
}

int mw_register_dma_memory_client(struct mw_dma_memory_client *client)
{
    int ret = 0;
    struct mw_dma_mem_client_internal *cinternal;

    if (mw_dma_memory_check_mandatory(client))
        return OS_RETURN_INVAL;

    if (client->mem_type < 1 || client->mem_type > MW_DMA_MEMORY_MAX_CLIENT)
        return OS_RETURN_INVAL;

    cinternal = &(g_mw_dma_clients[client->mem_type - 1]);

    os_mutex_lock(cinternal->mutex);

    if (cinternal->client != NULL) {
        ret = OS_RETURN_ERROR;
        goto out;
    }

    cinternal->client = client;
    cinternal->ref_count = 0;

out:
    os_mutex_unlock(cinternal->mutex);
    return ret;
}
#if defined(__linux__)
EXPORT_SYMBOL(mw_register_dma_memory_client);
#endif

int mw_unregister_dma_memory_client(struct mw_dma_memory_client *client)
{
    int ret = 0;
    struct mw_dma_mem_client_internal *cinternal;

    if (client->mem_type < 1 || client->mem_type > MW_DMA_MEMORY_MAX_CLIENT)
        return OS_RETURN_INVAL;

    cinternal = &(g_mw_dma_clients[client->mem_type - 1]);

    os_mutex_lock(cinternal->mutex);
    if (cinternal->ref_count > 0) {
        ret = OS_RETURN_BUSY;
        goto out;
    }
    cinternal->client = NULL;
    cinternal->ref_count = 0;

out:
    os_mutex_unlock(cinternal->mutex);
    return ret;
}
#if defined(__linux__)
EXPORT_SYMBOL(mw_unregister_dma_memory_client);
#endif

int mw_dma_memory_create_desc(struct mw_dma_desc **desc,
                              int mem_type,
                              unsigned long addr, size_t size,
                              int direction,
                              void *private_data)
{
    struct mw_dma_mem_client_internal *cinternal;
    int ret = 0;

    if (desc == NULL)
        return OS_RETURN_INVAL;

    if (mem_type < 1 || mem_type > MW_DMA_MEMORY_MAX_CLIENT)
        return OS_RETURN_INVAL;

    cinternal = &(g_mw_dma_clients[mem_type - 1]);

    os_mutex_lock(cinternal->mutex);

    if (cinternal->client == NULL) {
        ret = OS_RETURN_ERROR;
        goto out;
    }

    ret = cinternal->client->create_dma_desc(desc, addr, size,
                                             direction, private_data);
    if (ret == 0) {
        (*desc)->client = cinternal->client;
        cinternal->ref_count++;
    }

out:
    os_mutex_unlock(cinternal->mutex);
    return ret;
}

int mw_dma_memory_destroy_desc(struct mw_dma_desc *desc)
{
    struct mw_dma_mem_client_internal *cinternal;
    int ret = 0;

    if (desc == NULL || desc->client == NULL)
        return OS_RETURN_INVAL;

    if (desc->client->mem_type < 1 || desc->client->mem_type > MW_DMA_MEMORY_MAX_CLIENT)
        return OS_RETURN_INVAL;

    cinternal = &(g_mw_dma_clients[desc->client->mem_type - 1]);

    os_mutex_lock(cinternal->mutex);
    if (cinternal->client != desc->client) {
        ret = OS_RETURN_INVAL;
        goto out;
    }
    cinternal->client->destroy_dma_desc(desc);
    cinternal->ref_count--;

out:
    os_mutex_unlock(cinternal->mutex);
    return ret;
}

int mw_dma_memory_init(void)
{
    int i;
    int ret = 0;

    for (i = 0; i < MW_DMA_MEMORY_MAX_CLIENT; i++) {
        g_mw_dma_clients[i].client = NULL;
        g_mw_dma_clients[i].ref_count = 0;
        g_mw_dma_clients[i].mutex = os_mutex_alloc();
        if (g_mw_dma_clients[i].mutex == NULL) {
            ret = OS_RETURN_NOMEM;
            goto lock_err;
        }
    }

    ret = mw_register_dma_memory_client(&kernel_dma_client);
    if (ret != OS_RETURN_SUCCESS)
        goto kernel_err;
    ret = mw_register_dma_memory_client(&user_dma_client);
    if (ret != OS_RETURN_SUCCESS)
        goto user_err;
    ret = mw_register_dma_memory_client(&phy_dma_client);
    if (ret != OS_RETURN_SUCCESS)
        goto phy_err;

    return 0;

phy_err:
    mw_unregister_dma_memory_client(&user_dma_client);
user_err:
    mw_unregister_dma_memory_client(&kernel_dma_client);
kernel_err:
lock_err:
    for (i = 0; i < MW_DMA_MEMORY_MAX_CLIENT; i++) {
        if (g_mw_dma_clients[i].mutex != NULL) {
            os_mutex_free(g_mw_dma_clients[i].mutex);
            g_mw_dma_clients[i].mutex = NULL;
        }
    }
    return ret;
}

void mw_dma_memory_deinit(void)
{
    int i;

    mw_unregister_dma_memory_client(&phy_dma_client);
    mw_unregister_dma_memory_client(&user_dma_client);
    mw_unregister_dma_memory_client(&kernel_dma_client);

    for (i = 0; i < MW_DMA_MEMORY_MAX_CLIENT; i++) {
        if (g_mw_dma_clients[i].mutex != NULL) {
            os_mutex_free(g_mw_dma_clients[i].mutex);
            g_mw_dma_clients[i].mutex = NULL;
        }
    }
}
