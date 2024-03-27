////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////
#ifndef __MW_SG_H__
#define __MW_SG_H__

#pragma pack(push)
#pragma pack(1)

typedef unsigned long long mw_physical_addr_t;

#define MW_PHYSICAL_HIGH32(addr)    (((addr) >> 32) & UINT_MAX)
#define MW_PHYSICAL_LOW32(addr)     ((addr) & UINT_MAX)

typedef struct _mw_scatterlist_t {
    mw_physical_addr_t  address;
    unsigned long       length;
} mw_scatterlist_t;

#define mw_sg_dma_address(sg)  ((sg)->address)
#define mw_sg_dma_len(sg)      ((sg)->length)

static inline mw_scatterlist_t *mw_sg_next(mw_scatterlist_t *sg)
{
    return (++sg);
}

#pragma pack(pop)

#endif /* __MW_SG_H__ */
