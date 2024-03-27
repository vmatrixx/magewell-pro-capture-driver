////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////

#ifndef __MW_IOCTL_H__
#define __MW_IOCTL_H__

#include "ospi/ospi.h"

#include "supports/shared-image-buffer.h"
#include "picopng/picopng.h"

#include "mw-procapture-extension.h"
#include "mw-linux.h"

#include "mw-capture-impl.h"

struct OSD_IMAGE {
    os_mutex_t                              mutex;

    /* upload from user set png */
    struct xi_image_buffer                  imgbuf;
    RECT                                    rects[MWCAP_VIDEO_MAX_NUM_OSD_RECTS];
    int                                     rect_count;

    /* user set image create by MWCAP_IOCTL_VIDEO_CREATE_IMAGE */
    MWCAP_VIDEO_OSD_IMAGE                   user_image;
};

static inline bool _osd_image_is_valid(struct OSD_IMAGE *image)
{
    return image->rect_count != 0 && xi_image_buffer_isvalid(&image->imgbuf);
}

/* user set a png picture */
struct OSD_IMAGE_DATA {
    png_pix_info_t                          png_info;
    int                                     rect_count;
    RECT                                    rects[MWCAP_VIDEO_MAX_NUM_OSD_RECTS];
};

static inline bool _osd_image_data_is_valid(struct OSD_IMAGE_DATA *odata)
{
    return (odata->png_info.width != 0
            && odata->png_info.height !=0
            && karray_get_count(&odata->png_info.arr_image) !=0
            && karray_get_data(&odata->png_info.arr_image) != NULL
            && odata->rect_count != 0);
}

struct OSD_IMAGE_SETTINGS {
    int										cx;
    int										cy;
    MWCAP_VIDEO_COLOR_FORMAT				colorFormat;
    MWCAP_VIDEO_QUANTIZATION_RANGE			quantRange;
    MWCAP_VIDEO_SATURATION_RANGE			satRange;
};

struct frame_settings {
    short                                   brightness;
    short                                   contrast;
    short                                   hue;
    short                                   saturation;

    MWCAP_VIDEO_PROCESS_SETTINGS            process_settings;
};

struct mw_device {
    os_mutex_t                  dev_mutex;

    os_spinlock_t               list_lock;
    struct list_head            stream_list;
    int                         max_stream_id;
    int                         stream_count;

    /* default settings */
    os_spinlock_t               settings_lock;
    struct frame_settings       settings;
    MWCAP_VIDEO_OSD_SETTINGS    png_settings;

    os_dma_par_t                dma_priv;

    int                         m_iChannel;
};

struct mw_stream {
    void                            *driver;

    struct mw_device                *mw_dev;

    struct list_head                list_node;
    int                             stream_id;
    os_pid_t                        pid;
    char                            proc_name[MW_MAX_PROCESS_NAME_LEN];
    int                             ctrl_id;
    struct mw_stream                *ctrl_stream;


    /* report the os local format to user */
    MWCAP_VIDEO_CONNECTION_FORMAT   format;
    os_spinlock_t                   format_lock;

    /* settings */
    struct frame_settings           settings;
    os_spinlock_t                   settings_lock;

    os_mutex_t                      png_mutex;
    MWCAP_VIDEO_OSD_SETTINGS        png_settings; /* user set */
    struct OSD_IMAGE_DATA           png_data; /* png decoded data */
    struct OSD_IMAGE_SETTINGS       png_dest_settings; /* set after sucess upload */

    struct OSD_IMAGE                osd_image;

    struct mw_stream_cap            s_mwcap;
};

int mw_stream_init(struct mw_stream *stream,
                   void *driver,
                   struct mw_device *mw_dev,
                   os_pid_t pid,
                   char *proc_name);

void mw_stream_deinit(struct mw_stream *stream);

long mw_stream_ioctl(struct mw_stream *stream,  os_iocmd_t cmd, void *arg);


bool mw_stream_acquire_osd_image(struct mw_stream *stream, struct xi_image_buffer **pimage,
                           RECT *rects, int *rect_count);

void mw_stream_release_osd_image(struct mw_stream *stream);

void mw_stream_update_osd_image(struct mw_stream *stream);

void mw_stream_update_connection_format(struct mw_stream *stream,
                                      bool update_osd,
                                      bool connected,
                                      int width,
                                      int height,
                                      u32 fourcc,
                                      u32 frame_duration);

#if defined(__linux__)
bool os_local_stream_is_generating(struct mw_stream *stream);
#else
bool mw_stream_is_local(struct mw_stream *stream);
#endif

int xi_v4l2_get_last_frame_sdianc_data(struct mw_stream *stream,
                                       MWCAP_SDI_ANC_PACKET **packets);

#endif /* __MW_IOCTL_H__ */
