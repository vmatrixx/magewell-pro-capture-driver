////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2017 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////
#ifndef __LINUX_FILE_H__
#define __LINUX_FILE_H__

#include <linux/fs.h>

struct file *linux_file_open(const char *path, int flags, int mode);

void linux_file_close(struct file *file);

ssize_t linux_file_read(struct file *file, loff_t offset, unsigned char *data, size_t size);

ssize_t linux_file_write(struct file *file, loff_t offset, unsigned char *data, size_t size);

unsigned int linux_file_get_size(struct file *file);

#endif /* __LINUX_FILE_H__ */
