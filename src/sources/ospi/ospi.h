////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////
#ifndef __OSPI_H__
#define __OSPI_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#if defined(__APPLE__)

#include "ospi-mac.h"
#include "sg-mac.h"

#ifndef __iomem
#define __iomem
#endif

#ifndef INT_MAX
#define INT_MAX INT32_MAX
#endif

#ifndef UINT_MAX
#define UINT_MAX UINT32_MAX
#endif

typedef uint8_t    u8;
typedef uint16_t   u16;
typedef uint32_t   u32;
typedef uint64_t   u64;

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:        the pointer to the member.
 * @type:       the type of the container struct this is embedded in.
 * @member:     the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({                      \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);        \
    (type *)( (char *)__mptr - offsetof(type,member) );})

#include "os-list.h"

#elif defined(__linux__)

#include "ospi-linux.h"
#include <linux/list.h>

#else
#error "Error: Not support system!"
#endif

#ifdef __PTRDIFF_TYPE__
    typedef __PTRDIFF_TYPE__    os_ptrdiff_t;
#else
    typedef long                os_ptrdiff_t;
#endif

#ifdef __UINTPTR_TYPE__
    typedef __UINTPTR_TYPE__    os_uintptr_t;
#else
    typedef unsigned long       os_uintptr_t;
#endif

/* module debug */
extern unsigned int debug_level;
    
#define XI_DEBUG_ENABLE
#ifdef 	XI_DEBUG_ENABLE
    #define xi_debug(level, fmt, ...) { 		      \
        if (debug_level > level) 					  \
            os_printf("[%d:%24s]" fmt, 		  \
                __LINE__ ,__func__, ##__VA_ARGS__);  \
        }
    #else
#define xi_debug(level, fmt, ...) do {} while(0)
#endif


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __OSPI_H__ */
