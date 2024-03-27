////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////

#ifndef __KARRAY_H__
#define __KARRAY_H__

#include "ospi/ospi.h"

/* kernel dynamically expanding array */

struct karray {
    char            *data;
    int             count;
    int             max_count;
    int             element_size;
    int             alloc_size;
};

static inline bool karray_is_empty(struct karray *array)
{
    return (array->data == NULL || array->count == 0
            || array->max_count == 0
            || array->element_size == 0
            );
}

void karray_init(struct karray *array, long element_size);
void karray_free(struct karray *array);

static inline void *karray_get_data(struct karray *array)
{
    return array->data;
}

static inline void *karray_get_element(struct karray *array, long element_nr)
{
    if (karray_is_empty(array) || element_nr >= array->count)
        return NULL;

    return &array->data[element_nr * array->element_size];
}

int karray_set_element(struct karray *array, long element_nr, void *src);

static inline int karray_get_count(struct karray *array)
{
    return array->count;
}

static inline long karray_get_max_count(struct karray *array)
{
    return array->max_count;
}

static inline long karray_get_size(struct karray *array)
{
    return array->count * array->element_size;
}

bool karray_set_count(struct karray *array, long count, bool keep_orig);

bool karray_copy(struct karray *array, void *src, long count);

bool karray_append(struct karray *array, void *src, long count);

void karray_delete_element(struct karray *array, long element_nr);

#endif /* __KARRAY_H__ */

