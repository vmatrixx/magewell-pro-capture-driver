////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////

#ifndef __MW_CAPTURE_IMPL_H__
#define __MW_CAPTURE_IMPL_H__

#include "ospi/ospi.h"

#include "supports/xi-notify-event.h"

struct mw_stream_cap {
    void                        *driver;
    os_dma_par_t                dma_priv;

    os_spinlock_t               timer_lock;
    struct list_head            timer_list;
    os_spinlock_t               notify_lock;
    struct list_head            notify_list;

    /* pined buffers */
    os_spinlock_t               pin_buf_lock;
    struct list_head            pin_buf_list;

    /* video capture */
    os_mutex_t                  video_mutex;
    unsigned long               generating;
    os_spinlock_t               req_lock;
    os_thread_t                 kthread;
    struct list_head            req_list;
    os_event_t                  req_event;
    struct list_head            completed_list;
    os_event_t                  video_done; // from user
    bool                        exit_capture_thread;

    xi_notify_event             *low_latency_notify; // for low latency
    int                         m_iFrameBuffered;
    int                         m_iFrameBuffering;

    /* audio capture */
    os_mutex_t                  audio_mutex;
    unsigned long               audio_open;
    int                         prev_audio_block_id;
    xi_notify_event             *audio_notify;
    bool                        m_bAudioAcquired;

    /* video upload */
    os_mutex_t                  upload_mutex;
    struct list_head            upload_buf_list;

    int                         m_iChannel;
};

int mw_capture_init(struct mw_stream_cap *mwcap, void *driver, int iChannel, os_dma_par_t dma_priv);

void mw_capture_deinit(struct mw_stream_cap *mwcap);

bool mw_capture_ioctl(struct mw_stream_cap *mwcap, os_iocmd_t cmd,
                      void *arg, long *ret_val);


static inline bool mw_capture_is_generating(struct mw_stream_cap *mwcap)
{
    return os_test_bit(0, &mwcap->generating);
}

#endif /* __MW_CAPTURE_IMPL_H__ */
