////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////
#ifndef __XI_CAPTURE_H__
#define __XI_CAPTURE_H__

#define VIDEO_CAP_DRIVER_NAME "Pro Capture"

#include "picopng/picopng.h"

int loadPngImage(const char *filename, png_pix_info_t *dinfo);

#endif
