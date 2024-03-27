////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2017 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////

#include "linux-file.h"

#include <linux/sched.h>
#include <linux/version.h>
#include <linux/uaccess.h>

struct file *linux_file_open(const char *path, int flags, int mode)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0)
    struct file *filp = NULL;
    mm_segment_t oldfs;

    oldfs = get_fs();
    set_fs(KERNEL_DS);
    filp = filp_open(path, flags, mode);
    set_fs(oldfs);

    return filp;
#else
    return filp_open(path, flags, mode);
#endif
}

void linux_file_close(struct file *file)
{
    filp_close(file, NULL);
}

ssize_t linux_file_read(struct file *file, loff_t offset, unsigned char *data, size_t size)
{
    ssize_t ret;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
    ret = kernel_read(file, data, size, &offset);
#else
    mm_segment_t oldfs;

    oldfs = get_fs();
    set_fs(KERNEL_DS);
    ret = vfs_read(file, data, size, &offset);
    set_fs(oldfs);
#endif

    return ret;
}

ssize_t linux_file_write(struct file *file, loff_t offset, unsigned char *data, size_t size)
{
    ssize_t ret;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
    ret = kernel_write(file, data, size, &offset);
#else
    mm_segment_t oldfs;

    oldfs = get_fs();
    set_fs(KERNEL_DS);
    ret = vfs_write(file, data, size, &offset);
    set_fs(oldfs);
#endif

    return ret;
}

unsigned int linux_file_get_size(struct file *file)
{
    struct kstat stat;
    int ret;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0)
    ret = vfs_getattr(&file->f_path, &stat, STATX_BASIC_STATS, AT_STATX_SYNC_AS_STAT);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,9,0)
    ret = vfs_getattr(&file->f_path, &stat);
#else
    ret = vfs_getattr(file->f_path.mnt, file->f_path.dentry, &stat);
#endif

    if (ret != 0)
        return 0;

    return (unsigned int)stat.size;
}
