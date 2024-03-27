////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////
#include <linux/types.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/videodev2.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/dma-mapping.h>
#include <linux/pagemap.h>
#include <linux/version.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-common.h>

#include "ospi/ospi.h"

#include "capture.h"
#include "xi-driver.h"
#include "v4l2.h"
#include "mw-stream.h"
#include "mw-capture-impl.h"

#include "v4l2-sg-buf.h"

#define MIN_WIDTH  48
#define MIN_HEIGHT 32

/* ------------------------------------------------------------------
   Basic structures
   ------------------------------------------------------------------*/

struct xi_fmt {
    char  *name;
    u32    fourcc;          /* v4l2 format id */
    int    depth;
};

static struct xi_fmt formats[] = {
    {
        .name     = "4:2:2, packed, YUYV",
        .fourcc   = V4L2_PIX_FMT_YUYV,
        .depth    = 16,
    },
    {
        .name     = "4:2:2, packed, UYVY",
        .fourcc   = V4L2_PIX_FMT_UYVY,
        .depth    = 16,
    },
    {
        .name     = "4:2:0, NV12",
        .fourcc   = V4L2_PIX_FMT_NV12,
        .depth    = 12,
    },
    {
        .name     = "4:2:0, planar, YV12",
        .fourcc   = V4L2_PIX_FMT_YVU420,
        .depth    = 12,
    },
    {
        .name     = "4:2:0, planar, I420",
        .fourcc   = V4L2_PIX_FMT_YUV420,
        .depth    = 12,
    },
    {
        .name     = "RGB24",
        .fourcc   = V4L2_PIX_FMT_RGB24,
        .depth    = 24,
    },
    {
        .name     = "RGB32",
        .fourcc   = V4L2_PIX_FMT_RGB32,
        .depth    = 32,
    },
    {
        .name     = "BGR24",
        .fourcc   = V4L2_PIX_FMT_BGR24,
        .depth    = 24,
    },
    {
        .name     = "BGR32",
        .fourcc   = V4L2_PIX_FMT_BGR32,
        .depth    = 32,
    },
    {
        .name     = "Greyscale 8 bpp",
        .fourcc   = V4L2_PIX_FMT_GREY,
        .depth    = 8,
    },
    {
        .name     = "Greyscale 16 bpp",
        .fourcc   = V4L2_PIX_FMT_Y16,
        .depth    = 16,
    },
    {
        .name     = "32-bit, AYUV",
        .fourcc   = V4L2_PIX_FMT_YUV32,
        .depth    = 32,
    },
};

static struct xi_fmt *get_format(struct v4l2_format *f)
{
    struct xi_fmt *fmt;
    unsigned int k;

    for (k = 0; k < ARRAY_SIZE(formats); k++) {
        fmt = &formats[k];
        if (fmt->fourcc == f->fmt.pix.pixelformat)
            break;
    }

    if (k == ARRAY_SIZE(formats))
        return NULL;

    return &formats[k];
}

/* video info */
static struct xi_fmt             *g_def_fmt = &formats[0];
static unsigned int               g_def_width = 1920, g_def_height = 1080;
static unsigned int               g_def_frame_duration = 400000;

struct xi_stream_v4l2 {
    struct mutex                v4l2_mutex;
    unsigned long               generating;

    struct v4l2_sg_buf_queue    vsq;
    struct list_head            active;
    unsigned int                sequence;
    /* thread for generating video stream*/
    struct task_struct          *kthread;
    long long                   llstart_time;
    xi_timer                    *timer;
    bool                        exit_capture_thread;
    xi_notify_event             *notify;

    /* video info */
    struct xi_fmt               *fmt;
    unsigned int                width, height;
    unsigned int                stride;
    unsigned int                image_size;
    unsigned int                frame_duration;
};

struct xi_stream_pipe {
    struct mw_v4l2_channel      *mw_vch;

    /* sync for close */
    struct rw_semaphore         io_sem;

    struct xi_stream_v4l2       s_v4l2;

    struct mw_stream            s_mw;
};

static inline u32 fourcc_v4l2_to_mwcap(u32 fourcc)
{
    switch (fourcc) {
    case V4L2_PIX_FMT_NV12:
        return MWFOURCC_NV12;
    case V4L2_PIX_FMT_YVU420:
        return MWFOURCC_YV12;
    case V4L2_PIX_FMT_YUV420:
        return MWFOURCC_I420;
    case V4L2_PIX_FMT_UYVY:
        return MWFOURCC_UYVY;
    case V4L2_PIX_FMT_YUYV:
        return MWFOURCC_YUYV;
    case V4L2_PIX_FMT_RGB24:
        return MWFOURCC_RGB24;
    case V4L2_PIX_FMT_RGB32:
        return MWFOURCC_RGBA;
    case V4L2_PIX_FMT_BGR24:
        return MWFOURCC_BGR24;
    case V4L2_PIX_FMT_BGR32:
        return MWFOURCC_BGRA;
    case V4L2_PIX_FMT_GREY:
        return MWFOURCC_GREY;
    case V4L2_PIX_FMT_Y16:
        return MWFOURCC_Y16;
    case V4L2_PIX_FMT_YUV32:
        return MWFOURCC_AYUV;
    default:
        return MWFOURCC_NV12;
    }

    return MWFOURCC_NV12;
}

static inline bool v4l2_is_generating(struct xi_stream_v4l2 *s_v4l2)
{
    return test_bit(0, &s_v4l2->generating);
}

static void v4l2_process_one_frame(struct xi_stream_pipe *pipe, int iframe)
{
    struct mw_v4l2_channel *vch = pipe->mw_vch;
    struct xi_stream_v4l2 *s_v4l2 = &pipe->s_v4l2;
    struct v4l2_sg_buf *vbuf;
    int ret = -1;
    bool bottom_up;
    struct frame_settings settings;
    bool release_osd_mutex = false;
    struct xi_image_buffer *osd_image;
    RECT osd_rects[MWCAP_VIDEO_MAX_NUM_OSD_RECTS];
    int osd_count = 0;

    vbuf = v4l2_sg_queue_get_activebuf(&s_v4l2->vsq);
    if (vbuf == NULL)
        return;

    bottom_up = false;

    os_spin_lock(pipe->s_mw.settings_lock);
    memcpy(&settings, &pipe->s_mw.settings, sizeof(settings));
    os_spin_unlock(pipe->s_mw.settings_lock);

    release_osd_mutex = mw_stream_acquire_osd_image(&pipe->s_mw, &osd_image, osd_rects, &osd_count);

    ret = -1;
    vbuf->anc_packet_count = 0;
    if (vbuf->v4l2_buf.memory == V4L2_MEMORY_MMAP || vbuf->v4l2_buf.memory == V4L2_MEMORY_USERPTR) {
        if (NULL != vbuf->mwsg_list) {
            ret = xi_driver_sg_get_frame(
                        vch->driver,
                        vch->mw_dev.m_iChannel,
                        vbuf->mwsg_list,
                        vbuf->mwsg_len,
                        s_v4l2->stride,
                        iframe,
                        true,
                        osd_image,
                        osd_rects,
                        osd_count,
                        fourcc_v4l2_to_mwcap(s_v4l2->fmt->fourcc),
                        s_v4l2->width,
                        s_v4l2->height,
                        s_v4l2->fmt->depth,
                        bottom_up,
                        &settings.process_settings.rectSource, //src_rect
                        NULL,
                        settings.process_settings.nAspectX, //aspectx
                        settings.process_settings.nAspectY, //aspecty
                        settings.process_settings.colorFormat, //color_format
                        settings.process_settings.quantRange, //quant_range,
                        settings.process_settings.satRange, //sat_range
                        settings.contrast,
                        settings.brightness,
                        settings.saturation,
                        settings.hue,
                        settings.process_settings.deinterlaceMode, //deinterlace_mode
                        settings.process_settings.aspectRatioConvertMode,
                        0,
                        NULL,
                        0,
                        1000);
            if (iframe != -1) {
                int anc_count = 0;
                anc_count = xi_driver_get_sdianc_packets(
                                vch->driver,
                                vch->mw_dev.m_iChannel,
                                iframe,
                                ARRAY_SIZE(vbuf->anc_packets),
                                vbuf->anc_packets
                                );
                if (anc_count > 0)
                    vbuf->anc_packet_count = anc_count;
                xi_debug(20, "Got SDIANC data, count=%d\n", anc_count);
            }
        }
    }

    if (release_osd_mutex)
        mw_stream_release_osd_image(&pipe->s_mw);

    vbuf->v4l2_buf.field = V4L2_FIELD_NONE;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0))
    do_gettimeofday(&vbuf->v4l2_buf.timestamp);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(5,1,0))
    v4l2_get_timestamp(&vbuf->v4l2_buf.timestamp);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(5,6,0))
    vbuf->v4l2_buf.timestamp = ns_to_timeval(ktime_get_ns());
#else
    v4l2_buffer_set_timestamp(&vbuf->v4l2_buf, ktime_get_ns());
#endif
    vbuf->v4l2_buf.sequence = s_v4l2->sequence;
    s_v4l2->sequence++;
    if (ret == 0) {
        vbuf->state = V4L2_SG_BUF_STATE_DONE;
    } else { /* timeout */
        vbuf->state = V4L2_SG_BUF_STATE_ERROR;
    }

    v4l2_sg_queue_put_donebuf(&pipe->s_v4l2.vsq, vbuf);

    return;
}

static int xi_v4l2_thread_proc(void *data)
{
    struct xi_stream_pipe *pipe = data;
    struct xi_stream_v4l2 *s_v4l2 = &pipe->s_v4l2;
    long long lltime;
    long ret;

    VIDEO_SIGNAL_STATUS signal_status;
    bool low_latency_mode = false;

    os_event_t wait_events[] = {
        s_v4l2->timer->event,
        s_v4l2->notify->event
    };

    xi_debug(5, "v4l2 thread started\n");

    set_freezable();

    xi_driver_notify_event_get_status_bits(pipe->mw_vch->driver, pipe->mw_vch->mw_dev.m_iChannel, s_v4l2->notify);

    xi_driver_get_video_signal_status(pipe->mw_vch->driver, pipe->mw_vch->mw_dev.m_iChannel, &signal_status);

    lltime = s_v4l2->llstart_time + s_v4l2->frame_duration;
    xi_driver_schedual_timer(pipe->mw_vch->driver, lltime, s_v4l2->timer);

    for (;;) {
        MWCAP_VIDEO_BUFFER_INFO bufferInfo;

        ret = os_event_wait_for_multiple(
                    wait_events, ARRAY_SIZE(wait_events), 1000);
        if (s_v4l2->exit_capture_thread)
            break;

        if (ret == 0 || ret == -ERESTARTSYS) {
            xi_debug(0, "xi_event_wait_for_multiple ret=%ld\n", ret);
            continue;
        }

        os_spin_lock(pipe->s_mw.settings_lock);
        low_latency_mode = pipe->s_mw.settings.process_settings.bLowLatency
                && signal_status.pubStatus.state == MWCAP_VIDEO_SIGNAL_LOCKED;
        os_spin_unlock(pipe->s_mw.settings_lock);

        xi_driver_get_video_buffer_info(pipe->mw_vch->driver, pipe->mw_vch->mw_dev.m_iChannel, &bufferInfo);

        if (os_event_try_wait(s_v4l2->notify->event)) {
            u64 status_bits = xi_driver_notify_event_get_status_bits(
                        pipe->mw_vch->driver, pipe->mw_vch->mw_dev.m_iChannel, s_v4l2->notify);

            if (status_bits & MWCAP_NOTIFY_VIDEO_SIGNAL_CHANGE) {
                xi_driver_get_video_signal_status(
                            pipe->mw_vch->driver, pipe->mw_vch->mw_dev.m_iChannel, &signal_status);

                os_spin_lock(pipe->s_mw.settings_lock);
                low_latency_mode =
                        pipe->s_mw.settings.process_settings.bLowLatency
                        && (signal_status.pubStatus.state
                            == MWCAP_VIDEO_SIGNAL_LOCKED);
                os_spin_unlock(pipe->s_mw.settings_lock);
            }

            if (low_latency_mode
                    && (status_bits & MWCAP_NOTIFY_VIDEO_FRAME_BUFFERING)) {
                v4l2_process_one_frame(pipe,
                                       bufferInfo.iNewestBuffering);
            }
        }

        if (os_event_try_wait(s_v4l2->timer->event)) {
            if (!low_latency_mode)
                v4l2_process_one_frame(pipe,
                                       bufferInfo.iNewestBufferedFullFrame);

            lltime += s_v4l2->frame_duration;
            xi_driver_schedual_timer(pipe->mw_vch->driver, lltime, s_v4l2->timer);
        }

        try_to_freeze();
    }

    xi_driver_cancel_timer(pipe->mw_vch->driver, s_v4l2->timer);

    /* sync with kthread_stop */
    for (;;) {
        if (kthread_should_stop())
            break;
        msleep(1);
    }

    xi_debug(5, "v4l2 thread: exit\n");
    return 0;
}

static int xi_v4l2_stop_thread(struct xi_stream_pipe *pipe)
{
    struct xi_stream_v4l2 *s_v4l2 = &pipe->s_v4l2;

    s_v4l2->exit_capture_thread = true;
    if (s_v4l2->kthread) {
        xi_driver_cancel_timer(pipe->mw_vch->driver, s_v4l2->timer);
        os_event_set(s_v4l2->timer->event);
        kthread_stop(s_v4l2->kthread);
        s_v4l2->kthread = NULL;
    }

    if (!IS_ERR_OR_NULL(s_v4l2->notify)) {
        xi_driver_delete_notify_event(pipe->mw_vch->driver, pipe->mw_vch->mw_dev.m_iChannel, s_v4l2->notify);
        s_v4l2->notify = NULL;
    }

    if (s_v4l2->timer != NULL) {
        xi_driver_delete_timer(pipe->mw_vch->driver, s_v4l2->timer);
        s_v4l2->timer = NULL;
    }

    return 0;
}

static int xi_v4l2_start_thread(struct xi_stream_pipe *pipe)
{
    struct xi_stream_v4l2 *s_v4l2 = &pipe->s_v4l2;
    struct mw_v4l2_channel *vch = pipe->mw_vch;
    int ret = -1;

    s_v4l2->timer = xi_driver_new_timer(vch->driver, NULL);
    if (s_v4l2->timer == NULL) {
        ret = -EFAULT;
        goto err;
    }

    s_v4l2->notify = xi_driver_new_notify_event(
                vch->driver,
                vch->mw_dev.m_iChannel,
                MWCAP_NOTIFY_VIDEO_SIGNAL_CHANGE
                | MWCAP_NOTIFY_VIDEO_FRAME_BUFFERING,
                NULL
                );
    if (IS_ERR_OR_NULL(s_v4l2->notify)) {
        ret = IS_ERR(s_v4l2->notify) ? PTR_ERR(s_v4l2->notify) : -EFAULT;
        s_v4l2->notify = NULL;
        goto err;
    }

    os_event_clear(s_v4l2->timer->event);
    s_v4l2->llstart_time = xi_driver_get_time(vch->driver);

    s_v4l2->exit_capture_thread = false;
    s_v4l2->kthread = kthread_run(xi_v4l2_thread_proc, pipe, "XI-V4L2");
    if (IS_ERR(s_v4l2->kthread)) {
        xi_debug(0, "Create kernel thread XI-V4L2 Error: %m\n");
        ret = PTR_ERR(s_v4l2->kthread);
        s_v4l2->kthread = NULL;
        goto err;
    }

    return 0;

err:
    xi_v4l2_stop_thread(pipe);
    return ret;
}

static int xi_v4l2_start_generating(struct xi_stream_pipe *pipe)
{
    struct xi_stream_v4l2 *s_v4l2 = &pipe->s_v4l2;
    int ret = -1;

    xi_debug(5, "entering function %s\n", __func__);

    if (test_and_set_bit(0, &s_v4l2->generating))
        return -EBUSY;

    s_v4l2->sequence = 0;
    ret = v4l2_sg_queue_streamon(&s_v4l2->vsq);
    if (ret != 0) {
        goto err;
    }

    ret = xi_v4l2_start_thread(pipe);
    if (ret != 0)
        goto err;

    mw_stream_update_connection_format(&pipe->s_mw,
                                   true,
                                   v4l2_is_generating(s_v4l2),
                                   s_v4l2->width,
                                   s_v4l2->height,
                                   fourcc_v4l2_to_mwcap(s_v4l2->fmt->fourcc),
                                   s_v4l2->frame_duration
                                   );

    xi_debug(5, "returning from %s\n", __func__);
    return 0;

err:
    v4l2_sg_queue_streamoff(&s_v4l2->vsq);

    xi_v4l2_stop_thread(pipe);

    clear_bit(0, &s_v4l2->generating);
    return ret;
}

static void xi_v4l2_stop_generating(struct xi_stream_pipe *pipe)
{
    struct xi_stream_v4l2 *s_v4l2 = &pipe->s_v4l2;

    xi_debug(5, "entering function %s\n", __func__);

    if (!test_and_clear_bit(0, &s_v4l2->generating))
        return;

    /* shutdown control thread */
    xi_v4l2_stop_thread(pipe);

    /* 先停止线程，再清理queued buffer。因为在线程中，request frame之前会把queued buffer删除掉,
     * 但还没有给buffer设置状态，会认为这个buffer还是queued，会再次删除它，引起错误。 */
    v4l2_sg_queue_streamoff(&s_v4l2->vsq);

    mw_stream_update_connection_format(&pipe->s_mw,
                                   false,
                                   v4l2_is_generating(s_v4l2),
                                   s_v4l2->width,
                                   s_v4l2->height,
                                   fourcc_v4l2_to_mwcap(s_v4l2->fmt->fourcc),
                                   s_v4l2->frame_duration
                                   );
}

/* ------------------------------------------------------------------
   IOCTL vidioc handling
   ------------------------------------------------------------------*/
static int vidioc_querycap(struct file *file, void  *priv,
        struct v4l2_capability *cap)
{
    struct xi_stream_pipe *pipe = file->private_data;
    char driver_name[32];

    xi_debug(5, "entering function %s\n", __func__);

    if (xi_driver_get_family_name(pipe->mw_vch->driver, driver_name, sizeof(driver_name)) != 0)
        os_strlcpy(driver_name, VIDEO_CAP_DRIVER_NAME, sizeof(driver_name));

    os_strlcpy(cap->driver, driver_name, sizeof(cap->driver));
    if (xi_driver_get_card_name(pipe->mw_vch->driver, pipe->mw_vch->mw_dev.m_iChannel, cap->card, sizeof(cap->card)) <= 0) {
        os_strlcpy(cap->card, driver_name, sizeof(cap->card));
    }
    sprintf(cap->bus_info, "PCI:%s", dev_name(pipe->mw_vch->pci_dev));

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,17,0))
    cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
#else
    /* V4L2_CAP_DEVICE_CAPS must be set since linux kernle 3.17 */
    cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
    cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
#endif

    return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
        struct v4l2_fmtdesc *f)
{
    struct xi_fmt *fmt;
    xi_debug(5, "entering function %s\n", __func__);

    if (f->index >= ARRAY_SIZE(formats))
        return -EINVAL;

    fmt = &formats[f->index];

    strlcpy(f->description, fmt->name, sizeof(f->description));
    f->pixelformat = fmt->fourcc;

    return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
        struct v4l2_format *f)
{
    struct xi_stream_pipe *pipe = file->private_data;
    struct xi_stream_v4l2 *s_v4l2 = &pipe->s_v4l2;

    xi_debug(5, "entering function %s\n", __func__);

    f->fmt.pix.width        = s_v4l2->width;
    f->fmt.pix.height       = s_v4l2->height;
    f->fmt.pix.field        = s_v4l2->vsq.field;
    f->fmt.pix.pixelformat  = s_v4l2->fmt->fourcc;
    f->fmt.pix.bytesperline = s_v4l2->stride;
    f->fmt.pix.sizeimage    = s_v4l2->image_size;

    return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
        struct v4l2_format *f)
{
    struct xi_fmt *fmt;
    enum v4l2_field field;
    MWCAP_VIDEO_CAPS video_caps;
    struct xi_stream_pipe *pipe = file->private_data;
    struct mw_v4l2_channel *vch = pipe->mw_vch;

    xi_driver_get_video_caps(vch->driver, &video_caps);

    xi_debug(5, "entering function %s\n", __func__);

    fmt = get_format(f);
    if (!fmt) {
        xi_debug(1, "Fourcc format (0x%08x) invalid.\n",
                f->fmt.pix.pixelformat);
        return -EINVAL;
    }

    field = f->fmt.pix.field;

    if (field == V4L2_FIELD_ANY) {
        field = V4L2_FIELD_NONE;
    } else if (V4L2_FIELD_NONE != field) {
        xi_debug(1, "Field type invalid.\n");
        return -EINVAL;
    }

    f->fmt.pix.field = field;
    /* @walign @halign 2^align */
    if (fmt->fourcc == V4L2_PIX_FMT_YUYV
            || fmt->fourcc == V4L2_PIX_FMT_UYVY) {
        v4l_bound_align_image(&f->fmt.pix.width, MIN_WIDTH, video_caps.wMaxOutputWidth, 1,
                              &f->fmt.pix.height, MIN_HEIGHT, video_caps.wMaxOutputHeight, 0, 0);
    } else if (fmt->fourcc == V4L2_PIX_FMT_NV12
               || fmt->fourcc == V4L2_PIX_FMT_YVU420
               || fmt->fourcc == V4L2_PIX_FMT_YUV420) {
        v4l_bound_align_image(&f->fmt.pix.width, MIN_WIDTH, video_caps.wMaxOutputWidth, 1,
                              &f->fmt.pix.height, MIN_HEIGHT, video_caps.wMaxOutputHeight, 1, 0);
    } else {
        v4l_bound_align_image(&f->fmt.pix.width, MIN_WIDTH, video_caps.wMaxOutputWidth, 0,
                              &f->fmt.pix.height, MIN_HEIGHT, video_caps.wMaxOutputHeight, 0, 0);
    }

    /*
     * bytesperline不能接收用户设置的值，因为有些软件会传入不正确的值(如：mplayer的bytesperline
     * 是v4l2 G_FMT得到的，与现在要设置的分辨率不符).
     */
    if (fmt->fourcc == V4L2_PIX_FMT_NV12 ||
            fmt->fourcc == V4L2_PIX_FMT_YVU420 ||
            fmt->fourcc == V4L2_PIX_FMT_YUV420) {
        f->fmt.pix.bytesperline = f->fmt.pix.width;

        f->fmt.pix.sizeimage =
            (f->fmt.pix.height * f->fmt.pix.bytesperline * fmt->depth + 7) >> 3;
    } else { // packed
        f->fmt.pix.bytesperline = (f->fmt.pix.width * fmt->depth + 7) >> 3;

        f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
    }

    return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
        struct v4l2_format *f)
{
    struct xi_stream_pipe *pipe = file->private_data;
    struct xi_stream_v4l2 *s_v4l2 = &pipe->s_v4l2;
    int ret = 0;

    xi_debug(5, "entering function %s\n", __func__);

    if (v4l2_is_generating(s_v4l2)) {
        xi_debug(1, "%s device busy\n", __func__);
        ret = -EBUSY;
        goto out;
    }

    ret = vidioc_try_fmt_vid_cap(file, priv, f);
    if (ret < 0)
        return ret;

    s_v4l2->fmt = get_format(f);
    s_v4l2->width = f->fmt.pix.width;
    s_v4l2->height = f->fmt.pix.height;
    s_v4l2->vsq.field = f->fmt.pix.field;
    s_v4l2->stride = f->fmt.pix.bytesperline;
    s_v4l2->image_size = f->fmt.pix.sizeimage;

    mw_stream_update_connection_format(&pipe->s_mw,
                                   false,
                                   v4l2_is_generating(s_v4l2),
                                   s_v4l2->width,
                                   s_v4l2->height,
                                   fourcc_v4l2_to_mwcap(s_v4l2->fmt->fourcc),
                                   s_v4l2->frame_duration
                                   );

    ret = 0;

    printk("ProCapture: Set v4l2 format to %dx%d(%s)\n",
            f->fmt.pix.width, f->fmt.pix.height,
            s_v4l2->fmt->name);

out:
    return ret;
}

static int vidioc_reqbufs(struct file *file, void *priv,
        struct v4l2_requestbuffers *p)
{
    struct xi_stream_pipe *pipe = file->private_data;
    struct v4l2_sg_buf_queue *vsq = &pipe->s_v4l2.vsq;
    struct xi_stream_v4l2 *s_v4l2 = &pipe->s_v4l2;

    xi_debug(5, "entering function %s\n", __func__);

    return v4l2_sg_queue_reqbufs(vsq, p, s_v4l2->image_size);
}

/*
 * user get the video buffer status
 * for mmap, get the map offset
 * */
static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
    struct xi_stream_pipe *pipe = file->private_data;
    struct v4l2_sg_buf_queue *vsq = &pipe->s_v4l2.vsq;
    xi_debug(5, "entering function %s\n", __func__);

    return v4l2_sg_queue_querybuf(vsq, p);
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
    struct xi_stream_pipe *pipe = file->private_data;
    struct v4l2_sg_buf_queue *vsq = &pipe->s_v4l2.vsq;
    xi_debug(20, "entering function %s\n", __func__);

    return v4l2_sg_queue_qbuf(vsq, p);
}

/*
 * check if have available frame, if have no frame return <0;
 * if block, it will wait until there is frame available, (vb->done event).
 * if have frame, return 0 and transfer the buffer info to user.
 * */
static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
    struct xi_stream_pipe *pipe = file->private_data;
    struct v4l2_sg_buf_queue *vsq = &pipe->s_v4l2.vsq;
    xi_debug(20, "entering function %s\n", __func__);

    return v4l2_sg_queue_dqbuf(vsq, p, file->f_flags & O_NONBLOCK);
}

static int vidioc_enum_input(struct file *file, void *priv,
        struct v4l2_input *inp)
{
    struct xi_stream_pipe *pipe = file->private_data;
    const u32 *data;
    int count = 0;
    const char *name = "Auto";
    MWCAP_VIDEO_INPUT_TYPE input_type;
    typeof(inp->index) __index = inp->index;

    xi_debug(5, "entering function %s\n", __func__);

    memset(inp, 0, sizeof(*inp));
    inp->index = __index;

    data = xi_driver_get_supported_video_input_sources(
            pipe->mw_vch->driver, &count);

    if (inp->index > count || inp->index > 6/* input type number */)
        return -EINVAL;

    inp->type = V4L2_INPUT_TYPE_CAMERA;
    if (inp->index == 0)
        input_type = MWCAP_VIDEO_INPUT_TYPE_NONE;
    else
        input_type = INPUT_TYPE(data[inp->index-1]);

    switch (input_type) {
    case MWCAP_VIDEO_INPUT_TYPE_NONE:
        name = "Auto";
        break;
    case MWCAP_VIDEO_INPUT_TYPE_HDMI:
        name = "HDMI";
        break;
    case MWCAP_VIDEO_INPUT_TYPE_VGA:
        name = "VGA";
        break;
    case MWCAP_VIDEO_INPUT_TYPE_SDI:
        name = "SDI";
        break;
    case MWCAP_VIDEO_INPUT_TYPE_COMPONENT:
        name = "COMPONENT";
        break;
    case MWCAP_VIDEO_INPUT_TYPE_CVBS:
        name = "CVBS";
        break;
    case MWCAP_VIDEO_INPUT_TYPE_YC:
        name = "YC";
        break;
    }
    sprintf(inp->name, "%s", name);

    return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
    struct xi_stream_pipe *pipe = file->private_data;

    const u32 *data;
    int count = 0;
    u32 current_input;
    int index;

    xi_debug(5, "entering function %s\n", __func__);

    *i = 0;
    if (xi_driver_get_input_source_scan(pipe->mw_vch->driver)) {
        return 0;
    }

    data = xi_driver_get_supported_video_input_sources(
            pipe->mw_vch->driver, &count);

    current_input = xi_driver_get_video_input_source(pipe->mw_vch->driver);

    for (index = 0; index < count; index++) {
        if (data[index] == current_input) {
            *i = index + 1;
            break;
        }
    }

    return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
    struct xi_stream_pipe *pipe = file->private_data;

    const u32 *data;
    int count = 0;

    xi_debug(5, "entering function %s\n", __func__);

    data = xi_driver_get_supported_video_input_sources(
            pipe->mw_vch->driver, &count);
    if (i > count)
        return -EINVAL;

    if (i == 0) {
        xi_driver_set_input_source_scan(pipe->mw_vch->driver, true);
        return 0;
    }

    xi_driver_set_input_source_scan(pipe->mw_vch->driver, false);
    xi_driver_set_video_input_source(pipe->mw_vch->driver, data[i-1]);

    return (0);
}

static int vidioc_g_parm(struct file *file, void *__fh,
        struct v4l2_streamparm *sp)
{
    struct xi_stream_pipe *pipe = file->private_data;
    struct xi_stream_v4l2 *s_v4l2 = &pipe->s_v4l2;
    struct v4l2_captureparm *cp = &sp->parm.capture;

    cp->capability = V4L2_CAP_TIMEPERFRAME;
    cp->timeperframe.numerator = s_v4l2->frame_duration;
    cp->timeperframe.denominator = 10000000;

    return 0;
}

static int vidioc_s_parm(struct file *file, void *__fh,
        struct v4l2_streamparm *sp)
{
    struct xi_stream_pipe *pipe = file->private_data;
    struct xi_stream_v4l2 *s_v4l2 = &pipe->s_v4l2;
    struct v4l2_captureparm *cp = &sp->parm.capture;

    if ((cp->timeperframe.numerator == 0) ||
            (cp->timeperframe.denominator == 0)) {
        /* reset framerate */
        s_v4l2->frame_duration = 333333;
    } else {
        s_v4l2->frame_duration = (unsigned int)div_u64(10000000LL * cp->timeperframe.numerator,
                cp->timeperframe.denominator);
    }

    mw_stream_update_connection_format(&pipe->s_mw,
                                   false,
                                   v4l2_is_generating(s_v4l2),
                                   s_v4l2->width,
                                   s_v4l2->height,
                                   fourcc_v4l2_to_mwcap(s_v4l2->fmt->fourcc),
                                   s_v4l2->frame_duration
                                   );

    return 0;
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
    struct xi_stream_pipe *pipe = file->private_data;

    xi_debug(5, "entering function %s\n", __func__);

    if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    return xi_v4l2_start_generating(pipe);
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
    struct xi_stream_pipe *pipe = file->private_data;
    xi_debug(5, "entering function %s\n", __func__);

    if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    /* first kill thread, waiting for the delete queued buf */
    xi_v4l2_stop_generating(pipe);

    return 0;
}

/* --- controls ---------------------------------------------- */
static int vidioc_queryctrl(struct file *file, void *priv,
        struct v4l2_queryctrl *qc)
{
    switch (qc->id) {
    case V4L2_CID_BRIGHTNESS:
        return v4l2_ctrl_query_fill(qc, -100, 100, 1, 0);
    case V4L2_CID_CONTRAST:
        return v4l2_ctrl_query_fill(qc, 50, 200, 1, 100);
    case V4L2_CID_SATURATION:
        return v4l2_ctrl_query_fill(qc, 0, 200, 1, 100);
    case V4L2_CID_HUE:
        return v4l2_ctrl_query_fill(qc, -90, 90, 1, 0);
    }
    return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
        struct v4l2_control *ctrl)
{
    struct xi_stream_pipe *pipe = file->private_data;
    int ret = 0;

    os_spin_lock(pipe->s_mw.settings_lock);
    switch (ctrl->id) {
        case V4L2_CID_BRIGHTNESS:
            ctrl->value = pipe->s_mw.settings.brightness;
            break;
        case V4L2_CID_CONTRAST:
            ctrl->value = pipe->s_mw.settings.contrast;
            break;
        case V4L2_CID_SATURATION:
            ctrl->value = pipe->s_mw.settings.saturation;
            break;
        case V4L2_CID_HUE:
            ctrl->value = pipe->s_mw.settings.hue;
            break;
        default:
            ret = -EINVAL;
    }
    os_spin_unlock(pipe->s_mw.settings_lock);

    return ret;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
        struct v4l2_control *ctrl)
{
    struct v4l2_queryctrl qc;
    int err;
    struct xi_stream_pipe *pipe = file->private_data;
    int ret = 0;

    qc.id = ctrl->id;
    err = vidioc_queryctrl(file, priv, &qc);
    if (err < 0)
        return err;
    if (ctrl->value < qc.minimum || ctrl->value > qc.maximum)
        return -ERANGE;

    os_spin_lock(pipe->s_mw.settings_lock);
    switch (ctrl->id) {
        case V4L2_CID_BRIGHTNESS:
            pipe->s_mw.settings.brightness = ctrl->value;
            break;
        case V4L2_CID_CONTRAST:
            pipe->s_mw.settings.contrast = ctrl->value;
            break;
        case V4L2_CID_SATURATION:
            pipe->s_mw.settings.saturation = ctrl->value;
            break;
        case V4L2_CID_HUE:
            pipe->s_mw.settings.hue = ctrl->value;
            break;
        default:
            ret = -EINVAL;
    }
    os_spin_unlock(pipe->s_mw.settings_lock);

    return ret;
}

/* little value must put on top */
static struct v4l2_frmsize_discrete g_frmsize_array[] = {
    { 640, 360 },
    { 640, 480 },
    { 720, 480 },
    { 720, 576 },
    { 768, 576 },
    { 800, 600 },
    { 856, 480 },
    { 960, 540 },
    { 1024, 576 },
    { 1024, 768 },
    { 1280, 720 },
    { 1280, 800 },
    { 1280, 960 },
    { 1280, 1024 },
    { 1368, 768 },
    { 1440, 900 },
    { 1600, 1200 },
    { 1920, 1080 },
    { 1920, 1200 },
    { 2048, 1536 },

    { 2560, 1440 },
    { 3072, 768 },
    { 3840, 1024 },
    { 3840, 2160 },
    { 4096, 2160 },
};

extern int enum_framesizes_type;
static int vidioc_enum_framesizes(struct file *file, void *priv,
        struct v4l2_frmsizeenum *fsize)
{
    int i;
    MWCAP_VIDEO_CAPS video_caps;
    struct xi_stream_pipe *pipe = file->private_data;
    struct mw_v4l2_channel *vch = pipe->mw_vch;

    xi_driver_get_video_caps(vch->driver, &video_caps);

    for (i = 0; i < ARRAY_SIZE(formats); i++)
        if (formats[i].fourcc == fsize->pixel_format)
            break;
    if (i == ARRAY_SIZE(formats))
        return -EINVAL;

    switch (enum_framesizes_type) {
    case 1:
        if (fsize->index < 0 || fsize->index >= ARRAY_SIZE(g_frmsize_array))
            return -EINVAL;
        if (g_frmsize_array[fsize->index].width > video_caps.wMaxOutputWidth
                || g_frmsize_array[fsize->index].height > video_caps.wMaxOutputHeight)
            return -EINVAL;
        fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
        fsize->discrete.width = g_frmsize_array[fsize->index].width;
        fsize->discrete.height = g_frmsize_array[fsize->index].height;
        break;
    case 2:
        if (fsize->index != 0)
            return -EINVAL;
        fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
        fsize->stepwise.min_width = MIN_WIDTH;
        fsize->stepwise.min_height = MIN_HEIGHT;
        fsize->stepwise.max_width = video_caps.wMaxOutputWidth;
        fsize->stepwise.max_height = video_caps.wMaxOutputHeight;
        fsize->stepwise.step_width = 1;
        fsize->stepwise.step_height = 1;

        /* @walign @halign 2^align */
        if (fsize->pixel_format == V4L2_PIX_FMT_YUYV
                || fsize->pixel_format == V4L2_PIX_FMT_UYVY) {
            fsize->stepwise.step_width = 2;
            fsize->stepwise.step_height = 1;
        } else if (fsize->pixel_format == V4L2_PIX_FMT_NV12
                   || fsize->pixel_format == V4L2_PIX_FMT_YVU420
                   || fsize->pixel_format == V4L2_PIX_FMT_YUV420) {
            fsize->stepwise.step_width = 2;
            fsize->stepwise.step_height = 2;
        }
        break;
    default:
        if (fsize->index != 0)
            return -EINVAL;
        fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
        fsize->stepwise.min_width = MIN_WIDTH;
        fsize->stepwise.min_height = MIN_HEIGHT;
        fsize->stepwise.max_width = video_caps.wMaxOutputWidth;
        fsize->stepwise.max_height = video_caps.wMaxOutputHeight;
        fsize->stepwise.step_width = 1;
        fsize->stepwise.step_height = 1;
        break;
    }

    return 0;
}

extern unsigned int enum_frameinterval_min;
static int vidioc_enum_frameintervals(struct file *file, void *fh,
                   struct v4l2_frmivalenum *fival)
{
    int i;
    MWCAP_VIDEO_CAPS video_caps;
    struct xi_stream_pipe *pipe = file->private_data;
    struct mw_v4l2_channel *vch = pipe->mw_vch;

    if (fival->index != 0)
        return -EINVAL;

    for (i = 0; i < ARRAY_SIZE(formats); i++)
        if (formats[i].fourcc == fival->pixel_format)
            break;
    if (i == ARRAY_SIZE(formats))
        return -EINVAL;

    xi_driver_get_video_caps(vch->driver, &video_caps);

    switch (enum_framesizes_type) {
    case 1: /* V4L2_FRMSIZE_TYPE_DISCRETE */
        for (i = 0; i < ARRAY_SIZE(g_frmsize_array); i++)
            if (fival->width == g_frmsize_array[i].width
                && fival->height == g_frmsize_array[i].height)
                break;
        if (i == ARRAY_SIZE(g_frmsize_array))
            return -EINVAL;
        break;
    default: /* V4L2_FRMSIZE_TYPE_CONTINUOUS */
        if (fival->width < MIN_WIDTH
            || fival->width > video_caps.wMaxOutputWidth
            || fival->height < MIN_HEIGHT
            || fival->height > video_caps.wMaxOutputHeight)
            return -EINVAL;
        break;
    }

    fival->type = V4L2_FRMIVAL_TYPE_CONTINUOUS;
    fival->stepwise.min.denominator = 10000000;
    fival->stepwise.min.numerator =
            enum_frameinterval_min > 1000000 ? 1000000 : enum_frameinterval_min;
    fival->stepwise.max.denominator = 10000000;
    fival->stepwise.max.numerator = 10000000;
    fival->stepwise.step.denominator = 10000000;
    fival->stepwise.step.numerator = 1;

    return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0))
static int vidioc_cropcap(struct file *file, void *priv,
        struct v4l2_cropcap *ccap)
{
    struct xi_stream_pipe *pipe = file->private_data;
    struct mw_v4l2_channel *vch = pipe->mw_vch;

    VIDEO_SIGNAL_STATUS status;

    xi_driver_get_video_signal_status(vch->driver, vch->mw_dev.m_iChannel, &status);

    switch (ccap->type) {
        case V4L2_BUF_TYPE_VIDEO_CAPTURE:
            ccap->bounds.left = 0;
            ccap->bounds.top = 0;
            ccap->bounds.width = status.pubStatus.cx;
            ccap->bounds.height = status.pubStatus.cy;

            memcpy(&ccap->defrect, &ccap->bounds,
                    sizeof(struct v4l2_rect));

            ccap->pixelaspect.numerator = 1; //TODO
            ccap->pixelaspect.denominator = 1;
            break;
        case V4L2_BUF_TYPE_VIDEO_OVERLAY:
        default:
            return -EINVAL;
    }

    return 0;
}

static int vidioc_g_crop(struct file *file, void *priv,
        struct v4l2_crop *c)
{
    struct xi_stream_pipe *pipe = file->private_data;
    VIDEO_SIGNAL_STATUS status;
    int width, height;

    xi_driver_get_video_signal_status(pipe->mw_vch->driver, pipe->mw_vch->mw_dev.m_iChannel, &status);

    switch (c->type) {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:

        os_spin_lock(pipe->s_mw.settings_lock);
        width = pipe->s_mw.settings.process_settings.rectSource.right > status.pubStatus.cx ?
                    status.pubStatus.cx : pipe->s_mw.settings.process_settings.rectSource.right;
        height = pipe->s_mw.settings.process_settings.rectSource.bottom > status.pubStatus.cy ?
                    status.pubStatus.cy : pipe->s_mw.settings.process_settings.rectSource.bottom;

        if (width == 0 || height == 0) {
            c->c.left = 0;
            c->c.top = 0;
            c->c.width = status.pubStatus.cx;
            c->c.height = status.pubStatus.cy;
        } else {
            c->c.left = pipe->s_mw.settings.process_settings.rectSource.left;
            c->c.top = pipe->s_mw.settings.process_settings.rectSource.top;
            c->c.width = width;
            c->c.height = height;
        }
        os_spin_unlock(pipe->s_mw.settings_lock);

        break;
    case V4L2_BUF_TYPE_VIDEO_OVERLAY:
    default:
        return -EINVAL;
    }

    return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0))
static int vidioc_s_crop(struct file *file, void *priv,
        struct v4l2_crop *c)
#else
static int vidioc_s_crop(struct file *file, void *priv,
        const struct v4l2_crop *c)
#endif
{
    struct xi_stream_pipe *pipe = file->private_data;
    VIDEO_SIGNAL_STATUS status;

    xi_driver_get_video_signal_status(pipe->mw_vch->driver, pipe->mw_vch->mw_dev.m_iChannel, &status);

    switch (c->type) {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:

        os_spin_lock(pipe->s_mw.settings_lock);
        if (!(pipe->s_mw.settings.process_settings.rectSource.left == 0
             && pipe->s_mw.settings.process_settings.rectSource.top == 0
             && pipe->s_mw.settings.process_settings.rectSource.right == 0
             && pipe->s_mw.settings.process_settings.rectSource.bottom == 0
             && c->c.left == 0
             && c->c.top == 0
             && c->c.width == status.pubStatus.cx
             && c->c.height == status.pubStatus.cy)) {
            pipe->s_mw.settings.process_settings.rectSource.left = c->c.left;
            pipe->s_mw.settings.process_settings.rectSource.top = c->c.top;
            pipe->s_mw.settings.process_settings.rectSource.right
                    = c->c.width - c->c.left > 0 ? c->c.width - c->c.left : 0;
            pipe->s_mw.settings.process_settings.rectSource.bottom
                    = c->c.height - c->c.top > 0 ? c->c.height - c->c.top : 0;
        }
        os_spin_unlock(pipe->s_mw.settings_lock);

        break;
    case V4L2_BUF_TYPE_VIDEO_OVERLAY:
    default:
        return -EINVAL;
    }

    return 0;
}
#endif

static int _stream_v4l2_init(struct xi_stream_pipe *pipe)
{
    struct xi_stream_v4l2 *s_v4l2 = &pipe->s_v4l2;
    struct mw_v4l2_channel *vch = pipe->mw_vch;
    VIDEO_SIGNAL_STATUS status;

    xi_driver_get_video_signal_status(vch->driver, vch->mw_dev.m_iChannel, &status);

    s_v4l2->generating = 0;
    mutex_init(&s_v4l2->v4l2_mutex);

    v4l2_sg_queue_init(&s_v4l2->vsq,
                       V4L2_BUF_TYPE_VIDEO_CAPTURE,
                       &s_v4l2->v4l2_mutex, // will unlock when dqbuf is waiting
                       vch->pci_dev,
                       V4L2_FIELD_NONE
                       );

    /* init video dma queues */
    INIT_LIST_HEAD(&s_v4l2->active);

    s_v4l2->fmt = g_def_fmt;
    if (status.pubStatus.state == MWCAP_VIDEO_SIGNAL_LOCKED) {
        s_v4l2->width = status.pubStatus.cx;
        s_v4l2->height = status.pubStatus.cy;
        s_v4l2->frame_duration = status.pubStatus.dwFrameDuration;
    } else {
        s_v4l2->width = g_def_width;
        s_v4l2->height = g_def_height;
        s_v4l2->frame_duration = g_def_frame_duration;
    }
    if (s_v4l2->fmt->fourcc == V4L2_PIX_FMT_NV12 ||
            s_v4l2->fmt->fourcc == V4L2_PIX_FMT_YVU420 ||
            s_v4l2->fmt->fourcc == V4L2_PIX_FMT_YUV420) {
        s_v4l2->stride = s_v4l2->width;
        s_v4l2->image_size = (s_v4l2->width * s_v4l2->height * s_v4l2->fmt->depth + 7) >> 3;
    } else {
        s_v4l2->stride = (s_v4l2->width * s_v4l2->fmt->depth + 7) >> 3;
        s_v4l2->image_size = s_v4l2->stride * s_v4l2->height;
    }

    return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0))
static inline char *_get_task_comm(char *buf, struct task_struct *tsk)
{
    /* buf must be at least sizeof(tsk->comm) in size */
    task_lock(tsk);
    strncpy(buf, tsk->comm, sizeof(tsk->comm));
    task_unlock(tsk);
    return buf;
}
#endif

/* ------------------------------------------------------------------
   File operations for the device
   ------------------------------------------------------------------*/
static int xi_open(struct file *file)
{
    struct mw_v4l2_channel *vch = video_drvdata(file);
    struct xi_stream_pipe *pipe;
    char proc_name[TASK_COMM_LEN];
    int ret = 0;

    xi_debug(5, "entering function %s\n", __func__);

    if (file->private_data != NULL)
        return -EFAULT;

    pipe = kzalloc(sizeof(*pipe), GFP_KERNEL);
    if (pipe == NULL)
        return -ENOMEM;

    pipe->mw_vch = vch;

    init_rwsem(&pipe->io_sem);

    _stream_v4l2_init(pipe);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0))
    _get_task_comm(proc_name, current);
#else
    get_task_comm(proc_name, current);
#endif

    ret = mw_stream_init(&pipe->s_mw,
                         vch->driver,
                         &vch->mw_dev,
                         task_pid_nr(current),
                         proc_name);
    if (ret != 0) {
        kfree(pipe);
        return ret;
    }

    mw_stream_update_connection_format(&pipe->s_mw,
                                   false,
                                   v4l2_is_generating(&pipe->s_v4l2),
                                   pipe->s_v4l2.width,
                                   pipe->s_v4l2.height,
                                   fourcc_v4l2_to_mwcap(pipe->s_v4l2.fmt->fourcc),
                                   pipe->s_v4l2.frame_duration
                                   );

    file->private_data = pipe;

    return 0;
}

    static ssize_t
xi_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
    xi_debug(1, " capture card not support read!\n");

    return -1;
}

    static unsigned int
xi_poll(struct file *file, struct poll_table_struct *wait)
{
    struct xi_stream_pipe *pipe = file->private_data;
    struct xi_stream_v4l2 *s_v4l2 = &pipe->s_v4l2;
    unsigned int mask = 0;

    xi_debug(20, "entering function %s\n", __func__);

    down_read(&pipe->io_sem);

    mutex_lock(&s_v4l2->v4l2_mutex);
    if (v4l2_is_generating(s_v4l2))
        mask |= v4l2_sg_queue_poll(file, &s_v4l2->vsq, wait);
    mutex_unlock(&s_v4l2->v4l2_mutex);

    up_read(&pipe->io_sem);

    return mask;
}

static int xi_close(struct file *file)
{
    struct video_device  *vdev = video_devdata(file);
    struct xi_stream_pipe *pipe = file->private_data;
    struct xi_stream_v4l2 *s_v4l2 = &pipe->s_v4l2;

    xi_debug(5, "entering function %s\n", __func__);

    down_write(&pipe->io_sem);

    mutex_lock(&s_v4l2->v4l2_mutex);
    xi_v4l2_stop_generating(pipe);
    mutex_unlock(&s_v4l2->v4l2_mutex);

    mw_stream_deinit(&pipe->s_mw);

    v4l2_sg_queue_deinit(&s_v4l2->vsq);

    up_write(&pipe->io_sem);

    kfree(pipe);

    file->private_data = NULL;

    xi_debug(5, "close called (dev=%s)\n", video_device_node_name(vdev));
    return 0;
}

static int xi_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct xi_stream_pipe *pipe = file->private_data;
    int ret = 0;

    xi_debug(5, "entering function %s\n", __func__);

    down_read(&pipe->io_sem);
    ret = v4l2_sg_buf_mmap(&pipe->s_v4l2.vsq, vma);
    up_read(&pipe->io_sem);

    return ret;
}

static long _mw_stream_ioctl(struct file *file, unsigned int cmd, void *arg)
{
    struct xi_stream_pipe *pipe = file->private_data;
    long ret = 0;

    ret = mw_stream_ioctl(&pipe->s_mw, cmd, arg);

    return ret;
}

static long xi_ioctl(struct file *file, unsigned int cmd,
        unsigned long arg)
{
    struct xi_stream_pipe *pipe = file->private_data;
    struct xi_stream_v4l2 *s_v4l2 = &pipe->s_v4l2;
    long ret;

    down_read(&pipe->io_sem);

    if (_IOC_TYPE(cmd) == _IOC_TYPE(VIDIOC_QUERYCAP)) {
        mutex_lock(&s_v4l2->v4l2_mutex);
        ret = video_ioctl2(file, cmd, arg);
        mutex_unlock(&s_v4l2->v4l2_mutex);
    } else {
        ret = os_args_usercopy(file, cmd, arg, _mw_stream_ioctl);
    }

    /* the return value of ioctl for user is int  */
    if (ret > INT_MAX) {
        ret = 1;
    }
    up_read(&pipe->io_sem);

    return ret;
}

#ifdef CONFIG_COMPAT
static long xi_compat_ioctl32(struct file *file, unsigned int cmd,
        unsigned long arg)
{
    //struct xi_stream_pipe *pipe = file->private_data;
    long ret;

    ret = xi_ioctl(file, cmd, arg);

#if 0
    down_read(&pipe->io_sem);

    ret = video_usercopy(file, cmd, arg, _mw_stream_ioctl);

    /* the return value of ioctl for user is int  */
    if (ret > INT_MAX) {
        ret = 1;
    }
    up_read(&pipe->io_sem);
#endif

    return ret;
}
#endif

static const struct v4l2_file_operations xi_fops = {
    .owner		    = THIS_MODULE,
    .open           = xi_open,
    .read           = xi_read,
    .release        = xi_close,
    .poll		    = xi_poll,
    .unlocked_ioctl = xi_ioctl,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0))
#ifdef CONFIG_COMPAT
    .compat_ioctl32 = xi_compat_ioctl32,
#endif
#endif
    .mmap           = xi_mmap,
};

static const struct v4l2_ioctl_ops xi_ioctl_ops = {
    .vidioc_querycap          = vidioc_querycap,
    .vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
    .vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
    .vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
    .vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
    .vidioc_reqbufs           = vidioc_reqbufs,
    .vidioc_querybuf          = vidioc_querybuf,
    .vidioc_qbuf              = vidioc_qbuf,
    .vidioc_dqbuf             = vidioc_dqbuf,
    .vidioc_enum_input        = vidioc_enum_input,
    .vidioc_g_input           = vidioc_g_input,
    .vidioc_s_input           = vidioc_s_input,
    .vidioc_g_parm            = vidioc_g_parm,
    .vidioc_s_parm            = vidioc_s_parm,
    .vidioc_streamon          = vidioc_streamon,
    .vidioc_streamoff         = vidioc_streamoff,
    .vidioc_queryctrl         = vidioc_queryctrl,
    .vidioc_g_ctrl            = vidioc_g_ctrl,
    .vidioc_s_ctrl            = vidioc_s_ctrl,
    .vidioc_enum_framesizes   = vidioc_enum_framesizes,
    .vidioc_enum_frameintervals
                              = vidioc_enum_frameintervals,

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0))
    .vidioc_cropcap      	  = vidioc_cropcap,
    .vidioc_s_crop       	  = vidioc_s_crop,
    .vidioc_g_crop       	  = vidioc_g_crop,
#endif
};

static struct video_device xi_template = {
    .name		    = VIDEO_CAP_DRIVER_NAME,
    /*
     * The device_caps was added in version 4.7 .
     * And has been checked since 5.4 in drivers/media/v4l2-core/v4l2-dev.c:863
     */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,4,0))
    .device_caps    = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING,
#endif
    .fops           = &xi_fops,
    .ioctl_ops 	    = &xi_ioctl_ops,
    .release	    = video_device_release,
};

static int mw_device_init(struct mw_device *mw_dev, struct xi_v4l2_dev *dev)
{
    int ret;

    mw_dev->dev_mutex = os_mutex_alloc();
    mw_dev->list_lock = os_spin_lock_alloc();
    mw_dev->settings_lock = os_spin_lock_alloc();
    if (mw_dev->dev_mutex == NULL
            || mw_dev->list_lock == NULL
            || mw_dev->settings_lock == NULL) {
        ret = OS_RETURN_NOMEM;
        goto lock_err;
    }
    INIT_LIST_HEAD(&mw_dev->stream_list);
    mw_dev->max_stream_id = 0;
    mw_dev->stream_count = 0;
    mw_dev->settings.process_settings.deinterlaceMode =
            MWCAP_VIDEO_DEINTERLACE_BLEND;

    mw_dev->dma_priv = dev->parent_dev;

    return OS_RETURN_SUCCESS;

lock_err:
    if (mw_dev->dev_mutex != NULL)
        os_mutex_free(mw_dev->dev_mutex);
    if (mw_dev->list_lock != NULL)
        os_spin_lock_free(mw_dev->list_lock);
    if (mw_dev->settings_lock != NULL)
        os_spin_lock_free(mw_dev->settings_lock);

    mw_dev->dev_mutex = NULL;
    mw_dev->list_lock = NULL;
    mw_dev->settings_lock = NULL;

    return ret;
}

static void mw_device_deinit(struct mw_device *mw_dev)
{
    if (mw_dev->dev_mutex != NULL)
        os_mutex_free(mw_dev->dev_mutex);
    if (mw_dev->list_lock != NULL)
        os_spin_lock_free(mw_dev->list_lock);
    if (mw_dev->settings_lock != NULL)
        os_spin_lock_free(mw_dev->settings_lock);

    mw_dev->dev_mutex = NULL;
    mw_dev->list_lock = NULL;
    mw_dev->settings_lock = NULL;
}

static int xi_v4l2_register_channel(struct xi_v4l2_dev *dev, int iChannel)
{
    struct mw_v4l2_channel *vch;
    struct video_device *vfd;
    int ret = 0;

    vch = &dev->mw_vchs[iChannel];
    vch->driver = dev->driver;
    vch->pci_dev = dev->parent_dev;

    ret = mw_device_init(&vch->mw_dev, dev);
    if (ret != OS_RETURN_SUCCESS)
        goto mw_dev_err;
    vch->mw_dev.m_iChannel = iChannel;

    ret = -ENOMEM;
    vfd = video_device_alloc();
    if (!vfd)
        goto unreg_dev;

    *vfd = xi_template;
    if (xi_driver_get_card_name(dev->driver, iChannel, vfd->name, sizeof(vfd->name)) <= 0)
        snprintf(vfd->name, sizeof(vfd->name), "%s", dev->driver_name);
    vfd->v4l2_dev = &dev->v4l2_dev;

    vch->vfd = vfd;
    video_set_drvdata(vfd, vch);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5,7,0))
    ret = video_register_device(vfd, VFL_TYPE_GRABBER, -1);
#else
    ret = video_register_device(vfd, VFL_TYPE_VIDEO, -1);
#endif
    if (ret < 0)
        goto rel_vdev;

    v4l2_info(&dev->v4l2_dev, "V4L2 device registered as %s\n",
            video_device_node_name(vfd));

    return 0;

rel_vdev:
    video_device_release(vfd);
unreg_dev:
mw_dev_err:
    mw_device_deinit(&vch->mw_dev);
    kfree(vch);
    return ret;
}

static void xi_v4l2_unregister_channels(struct xi_v4l2_dev *dev)
{
    struct mw_v4l2_channel *vch;
    int i;

    for (i = 0; i < dev->mw_vchs_count; i++) {
        vch = &dev->mw_vchs[i];

        if (vch->vfd != NULL) {
            video_unregister_device(vch->vfd);
            vch->vfd = NULL;
        }

        mw_device_deinit(&vch->mw_dev);
    }
}

int xi_v4l2_init(struct xi_v4l2_dev *dev, void *driver, void *parent_dev)
{
    int ret;
    int i;

    dev->driver = driver;
    dev->parent_dev = parent_dev;

    if (xi_driver_get_family_name(driver, dev->driver_name, sizeof(dev->driver_name)) != 0)
        os_strlcpy(dev->driver_name, VIDEO_CAP_DRIVER_NAME, sizeof(dev->driver_name));

    snprintf(dev->v4l2_dev.name, sizeof(dev->v4l2_dev.name),
            "%s", dev->driver_name);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39))
    /*
     * v4l2_device_register @dev must be set to NULL
     * kernel version < 2.6.39, it will overwrite the parent_dev's drvdata:
     *
     *  if (dev_get_drvdata(dev))
     *      v4l2_warn(v4l2_dev, "Non-NULL drvdata on register\n");
     *  dev_set_drvdata(dev, v4l2_dev);
     *
     * */
    ret = v4l2_device_register(NULL, &dev->v4l2_dev);
    if (ret != 0)
        goto reg_err;
    dev->v4l2_dev.dev = parent_dev;
#else
    ret = v4l2_device_register(parent_dev, &dev->v4l2_dev);
    if (ret != 0)
        goto reg_err;
#endif

    dev->mw_vchs_count = xi_driver_get_channle_count(driver);
    dev->mw_vchs = kzalloc(sizeof(struct mw_v4l2_channel) * dev->mw_vchs_count, GFP_KERNEL);
    if (!dev->mw_vchs)
        goto vchs_alloc_err;

    for (i = 0; i < dev->mw_vchs_count; i++) {
        ret = xi_v4l2_register_channel(dev, i);
        if (ret)
            goto vch_reg_err;
    }

    return 0;

vch_reg_err:
    xi_v4l2_unregister_channels(dev);
    kfree(dev->mw_vchs);
vchs_alloc_err:
    dev->mw_vchs_count = 0;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39))
    dev->v4l2_dev.dev = NULL;
#endif
    v4l2_device_unregister(&dev->v4l2_dev);
reg_err:
    return ret;
}

int xi_v4l2_release(struct xi_v4l2_dev *dev)
{
    if (dev == NULL)
        return 0;

    xi_v4l2_unregister_channels(dev);

    if (dev->mw_vchs) {
        kfree(dev->mw_vchs);
        dev->mw_vchs = NULL;
    }

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39))
    dev->v4l2_dev.dev = NULL;
#endif
    v4l2_device_unregister(&dev->v4l2_dev);

    return 0;
}

#ifdef CONFIG_PM
int xi_v4l2_suspend(struct xi_v4l2_dev *dev)
{
    return 0;
}

int xi_v4l2_resume(struct xi_v4l2_dev *dev)
{
    struct mw_stream *_stream = NULL;
    struct list_head *pos;

    struct mw_v4l2_channel *vch;
    int i;

    for (i = 0; i < dev->mw_vchs_count; i++) {
        vch = &dev->mw_vchs[i];

        os_spin_lock(vch->mw_dev.list_lock);
        if (!list_empty(&vch->mw_dev.stream_list)) {
            list_for_each(pos, &vch->mw_dev.stream_list) {
                _stream = list_entry(pos, struct mw_stream, list_node);
                os_spin_unlock(vch->mw_dev.list_lock);
                mw_stream_update_osd_image(_stream);
                os_spin_lock(vch->mw_dev.list_lock);
            }
        }
        os_spin_unlock(vch->mw_dev.list_lock);
    }

    return 0;
}
#endif

bool os_local_stream_is_generating(struct mw_stream *stream)
{
    struct xi_stream_pipe *pipe = container_of(stream, struct xi_stream_pipe, s_mw);

    return v4l2_is_generating(&pipe->s_v4l2);
}

int xi_v4l2_get_last_frame_sdianc_data(struct mw_stream *stream,
                                       MWCAP_SDI_ANC_PACKET **packets)
{
    struct xi_stream_pipe *pipe = container_of(stream, struct xi_stream_pipe, s_mw);

    return v4l2_sg_get_last_frame_sdianc_data(&pipe->s_v4l2.vsq, packets);
}
