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
#include <linux/version.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/freezer.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/initval.h>

#include "ospi/ospi.h"

#include "audio-resample/resample.h"
#include "avstream/xi-driver.h"
#include "capture.h"
#include "alsa.h"

static unsigned int audio_devno;
static int index[SNDRV_CARDS]   = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS]    = SNDRV_DEFAULT_STR;
static int enable[SNDRV_CARDS]  = SNDRV_DEFAULT_ENABLE_PNP;

typedef struct _xi_card_driver {
    void                        *driver;
    int                         m_iChannel;

    /* capture thread */
    struct task_struct          *kthread;
    xi_notify_event             *notify_event;
    short                       audio_samples[MWCAP_AUDIO_SAMPLES_PER_FRAME * 8];
    bool                        thread_exit;
    /* resample */
    void                        *resampler;
    u32                         num_output_samples;
    void                        *resampled_buf;
    u32                         resampled_buf_size;
    /* output */
    u32                         sample_rate_out;
    u16                         channels;
    u16                         bits_per_sample;


    struct snd_pcm              *pcm;
    struct snd_pcm_substream    *substream;

    atomic_t                    trigger;

    spinlock_t                  ring_lock;
    uint32_t                    ring_wpos_byframes;
    uint32_t                    ring_size_byframes;
    uint32_t                    period_size_byframes;
    uint32_t                    period_used_byframes;

    /* work queue for alsa atomic context */
    struct workqueue_struct     *audio_wq;
    struct work_struct          start_work;
    struct work_struct          stop_work;
} xi_card_driver;

/**************************************
  file ops
 **************************************/
static struct snd_pcm_hardware audio_pcm_hardware = {
    .info 				=	(SNDRV_PCM_INFO_MMAP |
                             SNDRV_PCM_INFO_INTERLEAVED |
                             SNDRV_PCM_INFO_BLOCK_TRANSFER |
                             SNDRV_PCM_INFO_RESUME |
                             SNDRV_PCM_INFO_MMAP_VALID),
    .formats 			=	SNDRV_PCM_FMTBIT_S16_LE,
    .rates 				=	(SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_32000 |
            SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000),
    .rate_min 			=	16000,
    .rate_max 			=	96000,
    .channels_min 		=	2,
    .channels_max 		=	8,
    .buffer_bytes_max 	=	64*1024,
    .period_bytes_min 	=	512,
    .period_bytes_max 	=	16*1024,
    .periods_min 		=	2,
    .periods_max 		=	255,
};

static int _deliver_samples(xi_card_driver *drv, void *aud_data, u32 aud_len)
{
    struct snd_pcm_substream *substream = drv->substream;
    int elapsed = 0;
    unsigned int frames = aud_len / (2 * drv->channels);
    int copy_bytes = 0;
    unsigned long flags;

    if (frames == 0)
        return 0;

    if (!atomic_read(&drv->trigger)) {
        return 0;
    }

    if (drv->ring_wpos_byframes + frames > drv->ring_size_byframes) {
        copy_bytes = (drv->ring_size_byframes - drv->ring_wpos_byframes) * 2 * drv->channels;
        memcpy(substream->runtime->dma_area + drv->ring_wpos_byframes * 2 * drv->channels,
                aud_data, copy_bytes);
        memcpy(substream->runtime->dma_area, aud_data + copy_bytes, aud_len - copy_bytes);
    } else {
        memcpy(substream->runtime->dma_area + drv->ring_wpos_byframes * 2 * drv->channels,
                aud_data, aud_len);
    }

    spin_lock_irqsave(&drv->ring_lock, flags);
    drv->ring_wpos_byframes += frames;
    drv->ring_wpos_byframes %= drv->ring_size_byframes;

    /* check if a period available */
    elapsed = (drv->period_used_byframes + frames) / drv->period_size_byframes;

    drv->period_used_byframes += frames;
    drv->period_used_byframes %= drv->period_size_byframes;
    spin_unlock_irqrestore(&drv->ring_lock, flags);

    if (elapsed && substream) {
        /* snd_pcm_period_elapsed will call SNDRV_PCM_TRIGGER_STOP in somecase */
        snd_pcm_period_elapsed(drv->substream);
    }

    return frames * 2 * drv->channels;
}

static int _create_resampler(xi_card_driver *drv,
        u32 sample_rate_in,
        u32 sample_rate_out)
{
    drv->resampler = InitResampler(sample_rate_in, sample_rate_out,
                                   drv->channels/* channels 1|2 */, 2/* quality 0~3 */);
    if (NULL == drv->resampler)
        return -1;

    return 0;
}

static void _destroy_resampler(xi_card_driver *drv)
{
    if (NULL != drv->resampler) {
        FreeResampler(drv->resampler);
        drv->resampler = NULL;
    }
}

static void _audio_sample_rate_changed(xi_card_driver *drv, u32 sample_rate)
{
    xi_debug(1, "Audio Sample Rate Changed: %d\n", sample_rate);
    _destroy_resampler(drv);

    if (sample_rate != drv->sample_rate_out && sample_rate != 0) {
        _create_resampler(drv, sample_rate, drv->sample_rate_out);
    }
}


/* gain: db = 20 * log10(g_ausVolumeMapDB[index]/0x4000) */
/* db(g_ausVolumeMapDB[max]) = 10db
 * db(g_ausVolumeMapDB[index]=0x4000) = 0db
 * db(g_ausVolumeMapDB[min]=0x0000) = mute
 * db(g_ausVolumeMapDB[min]=0x0001) = -84.29
 * db(g_ausVolumeMapDB[min]=0x0002) = -78.27
 * db(g_ausVolumeMapDB[min]=0x0003) = -74.75
 * db(g_ausVolumeMapDB[min]=0x0004) = -72.25
 * db(g_ausVolumeMapDB[min]=0x0005) = -70.31
 * db(g_ausVolumeMapDB[min]=0x0006) = -68.73
 */
static unsigned short g_ausVolumeMapDB[] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001,
    0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
    0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0005, 0x0005, 0x0005, 0x0006, 0x0006, 0x0006, 0x0007, 0x0007,
    0x0008, 0x0008, 0x0009, 0x0009, 0x000A, 0x000A, 0x000B, 0x000C,
    0x000D, 0x000D, 0x000E, 0x000F, 0x0010, 0x0011, 0x0012, 0x0013,
    0x0014, 0x0015, 0x0017, 0x0018, 0x0019, 0x001B, 0x001D, 0x001E,
    0x0020, 0x0022, 0x0024, 0x0026, 0x0029, 0x002B, 0x002E, 0x0030,
    0x0033, 0x0036, 0x003A, 0x003D, 0x0041, 0x0045, 0x0049, 0x004D,
    0x0052, 0x0056, 0x005C, 0x0061, 0x0067, 0x006D, 0x0073, 0x007A,
    0x0082, 0x0089, 0x0092, 0x009A, 0x00A3, 0x00AD, 0x00B7, 0x00C2,
    0x00CE, 0x00DA, 0x00E7, 0x00F5, 0x0103, 0x0113, 0x0123, 0x0134,
    0x0146, 0x015A, 0x016E, 0x0184, 0x019B, 0x01B3, 0x01CD, 0x01E9,
    0x0206, 0x0224, 0x0245, 0x0267, 0x028C, 0x02B2, 0x02DB, 0x0307,
    0x0335, 0x0365, 0x0399, 0x03CF, 0x0409, 0x0447, 0x0487, 0x04CC,
    0x0515, 0x0562, 0x05B4, 0x060A, 0x0666, 0x06C7, 0x072E, 0x079B,
    0x080E, 0x0888, 0x090A, 0x0993, 0x0A24, 0x0ABE, 0x0B61, 0x0C0E,
    0x0CC5, 0x0D86, 0x0E53, 0x0F2D, 0x1013, 0x1107, 0x1209, 0x131B,
    0x143D, 0x1570, 0x16B5, 0x180D, 0x197A, 0x1AFD, 0x1C96, 0x1E48,
    0x2013, 0x21FA, 0x23FD, 0x261F, 0x2861, 0x2AC6, 0x2D4E, 0x2FFE,
    0x32D6, 0x35D9, 0x390A, 0x3C6B, 0x4000, 0x43CA, 0x47CF, 0x4C10,
    0x5092, 0x5558, 0x5A67, 0x5FC2, 0x656E, 0x6B71, 0x71CF, 0x788D,
    0x7FB2, 0x8743, 0x8F47, 0x97C4, 0xA0C2, 0xAA49, 0xB460, 0xBF10,
    0xCA62
};

static void _process_audio_frame(
        xi_card_driver *drv, BOOLEAN bLPCM,
        WORD wChannelValid,
        BYTE byChannelOffset,
        const MWCAP_AUDIO_CAPTURE_FRAME * pAudioFrame)
{
    struct parameter_t *pParameterManager = NULL;
    short *_resampled_buf = drv->resampled_buf;
    int i;
    int delived = 0;
    int avail = 0;
    BOOLEAN abMute[8];
    unsigned short ausVolume[8];
    int nChannels = drv->channels;
    short asSamples[8];
    int nSamples;
    int nAddrInc;
    const DWORD * pdwLeftSamples;
    const DWORD * pdwRightSamples;

    BOOLEAN bSrcIs16Channels = (pAudioFrame->dwFlags & MWCAP_AUDIO_16_CHANNELS) != 0;

    if (pAudioFrame->dwSyncCode != MWCAP_AUDIO_FRAME_SYNC_CODE) {
        xi_debug(0, "Error Audio Data\n");
        return;
    }

    pParameterManager = xi_driver_get_parameter_manager(drv->driver);

    for (i = 0; i < nChannels; i++) {
        int nVolumeIndex;
        int nVolume = parameter_GetVolume(pParameterManager, drv->m_iChannel, i);
        nVolume -= xi_driver_GetFrontEndVolume(drv->driver, drv->m_iChannel, i);
        nVolumeIndex = ((nVolume + 90 * 0x10000) >> 15);
        nVolumeIndex = max(0, (int)min((int)ARRAY_SIZE(g_ausVolumeMapDB), nVolumeIndex));
        ausVolume[i] = g_ausVolumeMapDB[nVolumeIndex];
        abMute[i] = parameter_GetMute(pParameterManager, drv->m_iChannel, i);
    }

    nSamples = bSrcIs16Channels ? (MWCAP_AUDIO_SAMPLES_PER_FRAME / 2) : MWCAP_AUDIO_SAMPLES_PER_FRAME;
    nAddrInc = bSrcIs16Channels ? 16 : 8;

    pdwLeftSamples = pAudioFrame->adwSamples + byChannelOffset;
    pdwRightSamples = pAudioFrame->adwSamples + (bSrcIs16Channels ? 8 : 4) + byChannelOffset;

    for (i = 0; i < nSamples; i++) {
        switch (nChannels) {
        case 7:
        case 8:
            if (wChannelValid & 0x08) {
                asSamples[6] = (short)(pdwLeftSamples[3] >> 16);
                asSamples[7] = (short)(pdwRightSamples[3] >> 16);
            }
            else
                asSamples[6] = asSamples[7] = 0;
            __attribute__ ((__fallthrough__));
        case 5:
        case 6:
            if (wChannelValid & 0x04) {
                asSamples[4] = (short)(pdwLeftSamples[2] >> 16);
                asSamples[5] = (short)(pdwRightSamples[2] >> 16);
            }
            else
                asSamples[4] = asSamples[5] = 0;
            __attribute__ ((__fallthrough__));
        case 3:
        case 4:
            if (wChannelValid & 0x02) {
                asSamples[2] = (short)(pdwLeftSamples[1] >> 16);
                asSamples[3] = (short)(pdwRightSamples[1] >> 16);
            }
            else
                asSamples[2] = asSamples[3] = 0;
            __attribute__ ((__fallthrough__));
        case 1:
        case 2:
        default:
            if (wChannelValid & 0x01) {
                asSamples[0] = (short)(pdwLeftSamples[0] >> 16);
                asSamples[1] = (short)(pdwRightSamples[0] >> 16);
            }
            else
                asSamples[0] = asSamples[1] = 0;
            break;
        }

        pdwLeftSamples += nAddrInc;
        pdwRightSamples += nAddrInc;

        if (!bLPCM)
            memcpy(&drv->audio_samples[i * nChannels], asSamples, 2 * nChannels);
        else {
            int j;
            for (j = 0; j < nChannels; j++) {
                int nSampleValue;

                if (abMute[j])
                    nSampleValue = 0;
                else
                    nSampleValue = ((int)asSamples[j] * ausVolume[j]) >> 14;

                drv->audio_samples[i * nChannels + j] =
                        (short)max(min(32767, nSampleValue), -32768);
            }
        }
    }

    if (NULL == _resampled_buf)
        return;

    if (bLPCM && drv->resampler) {
        int cMaxOutputs = GetMaxOutput(nSamples * drv->channels, drv->resampler);
        if (cMaxOutputs + drv->num_output_samples <= drv->resampled_buf_size / sizeof(short))
            drv->num_output_samples += Resample(drv->audio_samples,
                    nSamples * nChannels,
                    &_resampled_buf[drv->num_output_samples],
                    drv->resampler);
    } else if (nSamples * nChannels + drv->num_output_samples <=
            drv->resampled_buf_size / sizeof(short)) {
        memcpy(&_resampled_buf[drv->num_output_samples], drv->audio_samples,
                nSamples * sizeof(short) * nChannels);
        drv->num_output_samples += nSamples * nChannels;
    }

    while (drv->num_output_samples >= 2 /* make sure one frame(samples * 2) available */) {
        delived = _deliver_samples(drv, _resampled_buf, drv->num_output_samples * sizeof(short));
        if (delived == 0)
            break;

        avail = drv->num_output_samples * sizeof(short) - delived;
        if (avail)
            memmove(_resampled_buf, &_resampled_buf[(delived / sizeof(short))], avail);

        drv->num_output_samples -= (delived / sizeof(short));
    }
}

static int audio_capture_thread_proc(void *data)
{
    xi_card_driver *drv = data;
    AUDIO_SIGNAL_STATUS audio_signal_status;
    int pre_block_id;
    u64 status_bits;
    long ret;
    MWCAP_AUDIO_CAPTURE_FRAME *audio_frame;

    audio_frame = kzalloc(sizeof(*audio_frame), GFP_KERNEL);
    if (audio_frame == NULL)
        return -ENOMEM;

    drv->num_output_samples = 0;

    xi_driver_acquire_audio_capture(drv->driver);

    xi_driver_get_audio_signal_status(drv->driver, drv->m_iChannel, &audio_signal_status);
    _audio_sample_rate_changed(drv, audio_signal_status.pubStatus.dwSampleRate);

    pre_block_id = xi_driver_get_completed_audio_block_id(drv->driver);

    set_freezable();

    while (1) {
        ret = os_event_wait(drv->notify_event->event, -1);
        if (drv->thread_exit)
            break;

        if (ret <= 0)
            continue;

        status_bits = xi_driver_notify_event_get_status_bits(drv->driver,
                drv->m_iChannel, drv->notify_event);

        if (status_bits & MWCAP_NOTIFY_AUDIO_SIGNAL_CHANGE) {
            xi_driver_get_audio_signal_status(drv->driver, drv->m_iChannel, &audio_signal_status);

            xi_debug(20, "LPCM: %d, Sample Rate: %d, Bits Per Sample: %d, Channel Flags: %d\n",
                    audio_signal_status.pubStatus.bLPCM,
                    audio_signal_status.pubStatus.dwSampleRate,
                    audio_signal_status.pubStatus.cBitsPerSample,
                    audio_signal_status.pubStatus.wChannelValid
                    );

            drv->num_output_samples = 0;

            _audio_sample_rate_changed(drv, audio_signal_status.pubStatus.dwSampleRate);
        }

        if (status_bits & MWCAP_NOTIFY_AUDIO_INPUT_RESET) {
            pre_block_id = -1;
            xi_debug(5, "Reset Audio Buffer\n");
        }

        if (status_bits & MWCAP_NOTIFY_AUDIO_FRAME_BUFFERED) {
            int new_block_id = xi_driver_get_completed_audio_block_id(drv->driver);

            if (new_block_id >= 0) {
                while (pre_block_id != new_block_id) {

                    pre_block_id = (pre_block_id + 1) % AUDIO_FRAME_COUNT;
                    xi_driver_normalize_audio_capture_frame(
                                drv->driver,
                                &audio_signal_status,
                                xi_driver_get_audio_capture_frame(drv->driver, pre_block_id),
                                audio_frame
                                );

                    _process_audio_frame(drv, audio_signal_status.pubStatus.bLPCM,
                                         audio_signal_status.pubStatus.wChannelValid,
                                         audio_signal_status.byChannelOffset,
                                         audio_frame);
                }
            }
        }

        try_to_freeze();
    }

    xi_driver_release_audio_capture(drv->driver);

    /* sync with kthread_stop */
    for (;;) {
        if (kthread_should_stop())
            break;
        msleep(1);
    }
    xi_debug(0, "Stop audio capture\n");

    return 0;
}

static int _start_audio_capture(xi_card_driver *drv,
        u32 samples_per_sec, unsigned int channels)
{
    drv->sample_rate_out        = samples_per_sec;
    drv->channels               = channels;

    drv->resampled_buf_size = drv->sample_rate_out * 2/* sample bytes */ * channels/* channels */;
    drv->resampled_buf = vmalloc(drv->resampled_buf_size);
    if (drv->resampled_buf == NULL)
        return -ENOMEM;

    drv->notify_event = xi_driver_new_notify_event(
                drv->driver,
                drv->m_iChannel,
                MWCAP_NOTIFY_AUDIO_INPUT_RESET
                | MWCAP_NOTIFY_AUDIO_SIGNAL_CHANGE
                | MWCAP_NOTIFY_AUDIO_FRAME_BUFFERED,
                NULL);
    if (IS_ERR_OR_NULL(drv->notify_event))
        goto free_buf;

    drv->thread_exit = false;
    drv->kthread = kthread_run(audio_capture_thread_proc, drv, "XI-ALSA");
    if (IS_ERR_OR_NULL(drv->kthread)) {
        xi_debug(0, "Create kernel thread XI-ALSA Error: %m\n");
        goto del_notify;
    }

    return 0;

del_notify:
    xi_driver_delete_notify_event(drv->driver, drv->m_iChannel, drv->notify_event);
free_buf:
    vfree(drv->resampled_buf);
    drv->notify_event = NULL;
    drv->resampled_buf = NULL;
    return -1;
}

static void _stop_audio_capture(xi_card_driver *drv)
{
    drv->thread_exit = true;
    if (drv->notify_event != NULL) {
        os_event_set(drv->notify_event->event);
    }
    if (!IS_ERR_OR_NULL(drv->kthread)) {
        kthread_stop(drv->kthread);
        drv->kthread = NULL;
    }

    _destroy_resampler(drv);

    if (drv->resampled_buf != NULL) {
        vfree(drv->resampled_buf);
        drv->resampled_buf = NULL;
    }
    if (drv->notify_event != NULL) {
        xi_driver_delete_notify_event(drv->driver, drv->m_iChannel, drv->notify_event);
        drv->notify_event = NULL;
    }
}

static void _start_work_proc(struct work_struct *work)
{
    xi_card_driver *drv = container_of(work, xi_card_driver, start_work);
    struct snd_pcm_substream *substream = drv->substream;

    xi_debug(1, "START work\n");
    if (substream != NULL && substream->runtime != NULL)
        _start_audio_capture(drv, substream->runtime->rate, substream->runtime->channels);
}

static void _stop_work_proc(struct work_struct *work)
{
    xi_card_driver *drv = container_of(work, xi_card_driver, stop_work);

    xi_debug(1, "STOP work\n");
    _stop_audio_capture(drv);
}

/*
 * this callback is atomic.
 * it cannot call that functions which may sleep.
 * The trigger callback should be as minimal as possible
 * and this is the reason audio_kthread_xxxx remove away
 *
 */
static int audio_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
    xi_card_driver *drv = snd_pcm_substream_chip(substream);

    switch (cmd) {
    case SNDRV_PCM_TRIGGER_START:
        xi_debug(1, "START\n");

        drv->ring_wpos_byframes = 0;
        drv->period_used_byframes = 0;

        queue_work(drv->audio_wq, &drv->start_work);

        atomic_set(&drv->trigger, 1);
        return 0;

    case SNDRV_PCM_TRIGGER_RESUME:
        xi_debug(1, "RESUME \n");

        //queue_work(drv->audio_wq, &drv->start_work);

        atomic_set(&drv->trigger, 1);
        return 0;

    case SNDRV_PCM_TRIGGER_STOP:
        xi_debug(1, "STOP\n");
        if (atomic_read(&drv->trigger)) {
            atomic_set(&drv->trigger, 0);
            queue_work(drv->audio_wq, &drv->stop_work);
        }
        return 0;

    case SNDRV_PCM_TRIGGER_SUSPEND:
        xi_debug(1, "SUSPEND\n");
        if (atomic_read(&drv->trigger)) {
            atomic_set(&drv->trigger, 0);
            //queue_work(drv->audio_wq, &drv->stop_work);
        }
        return 0;

    default:
        return -EINVAL;
    }
}

/*
 * this  will be called each time snd_pcm_prepare() is called,
 * that this callback is now non-atomic
 * can use schedule-related functions safely in this callback
 * Be careful that this callback will be called many times at each setup
 */
static int audio_pcm_prepare(struct snd_pcm_substream *substream)
{
    unsigned long flags;
    xi_card_driver *drv = snd_pcm_substream_chip(substream);
    struct snd_pcm_runtime *runtime = substream->runtime;

    spin_lock_irqsave(&drv->ring_lock, flags);
    drv->ring_size_byframes = runtime->buffer_size;
    drv->ring_wpos_byframes = 0;
    drv->period_size_byframes = runtime->period_size;
    drv->period_used_byframes = 0;
    spin_unlock_irqrestore(&drv->ring_lock, flags);

    return 0;
}

/*
 * This callback is called when the PCM middle layer
 * inquires the current hardware position on the buffer.
 * The position must be returned in frames, ranging from 0 to buffer_size - 1.
 * This is called usually from the buffer-update routine in the pcm middle layer,
 * which is invoked when snd_pcm_period_elapsed() is called by timer
 * This callback is also atomic.
 */
static snd_pcm_uframes_t audio_pcm_pointer(struct snd_pcm_substream *substream)
{
    snd_pcm_uframes_t pos;
    xi_card_driver *drv = snd_pcm_substream_chip(substream);

    spin_lock(&drv->ring_lock);
    pos = drv->ring_wpos_byframes;
    spin_unlock(&drv->ring_lock);

    return pos;
}

/*
 * This is called when the hardware parameter (hw_params) is set up by the application
 * the buffer size, the period size, the format, etc. are defined for the pcm substream
 * hardware setups should be done in this callback, including the allocation of buffers
 * Parameters to be initialized are retrieved by params_xxx() macros.
 * Note that this and prepare callbacks may be called multiple times per initialization.
 * this callback is non-atomic
 */
static int audio_pcm_hw_params(struct snd_pcm_substream *substream,struct snd_pcm_hw_params *hw_params)
{
    return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

/*
 * This is called to release the resources allocated via hw_params.
 * This function is always called before the close callback is called
 * the callback may be called multiple times
 * Keep track whether the resource was already released
 */
static int audio_pcm_hw_free(struct snd_pcm_substream *substream)
{
    xi_card_driver *drv = snd_pcm_substream_chip(substream);

    atomic_set(&drv->trigger, 0);

    return snd_pcm_lib_free_pages(substream);
}

/*
 * This is called when a pcm substream is opened.
 * here have to initialize the runtime->hw record.
 * If the hardware configuration needs more constraints, set here
 */
static int audio_pcm_open(struct snd_pcm_substream *substream)
{
    /* snd_pcm_substream_chip: The macro reads substream->private_data, which is a copy of pcm->private_data. */
    xi_card_driver *drv = snd_pcm_substream_chip(substream);
    struct snd_pcm_runtime *runtime = substream->runtime;

    runtime->hw = audio_pcm_hardware;
    drv->substream = substream;

    return 0;
}

/*
 * this is called when a pcm substream is closed
 * Any private instance for a pcm substream allocated
 * in the open callback will be released here.
 */
static int audio_pcm_close(struct snd_pcm_substream *substream)
{
    return 0;
}

struct snd_pcm_ops audio_pcm_capture_ops = {
    .open       =   audio_pcm_open,
    .close      =   audio_pcm_close,
    .ioctl      =   snd_pcm_lib_ioctl,
    .hw_params  =   audio_pcm_hw_params,
    .hw_free    =   audio_pcm_hw_free,
    .prepare    =   audio_pcm_prepare,
    .trigger    =   audio_pcm_trigger,
    .pointer    =   audio_pcm_pointer,
};

static int alsa_create_pcm(struct snd_card *card, xi_card_driver *drv, char *driver_name)
{
    int err;
    struct snd_pcm *pcm;

    /* 0: pcm device 0, 0: no playback stream, 1: one capture stream, */
    err = snd_pcm_new(card, driver_name, 0, 0, 1, &pcm);
    if (err < 0)
        return err;

    snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &audio_pcm_capture_ops);

    pcm->private_data = drv;
    pcm->info_flags = 0;
    os_strlcpy(pcm->name, driver_name, sizeof(pcm->name));
    os_strlcat(pcm->name, " PCM", sizeof(pcm->name));

    snd_pcm_lib_preallocate_pages_for_all(
            pcm,
            SNDRV_DMA_TYPE_CONTINUOUS,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6,1,0))
			card->dev,
#else
            snd_dma_continuous_data(GFP_KERNEL),
#endif
            audio_pcm_hardware.buffer_bytes_max,
            audio_pcm_hardware.buffer_bytes_max
            );

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)
    snd_pcm_add_chmap_ctls(pcm, SNDRV_PCM_STREAM_CAPTURE,
                           snd_pcm_alt_chmaps, 8, 0, NULL);
#endif

    drv->pcm = pcm;

    return 0;
}


/****************************************************************************
                CONTROL INTERFACE
 ****************************************************************************/
static int snd_alsa_volume_info(struct snd_kcontrol *kcontrol,
                struct snd_ctl_elem_info *info)
{
    info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
    info->count = 8;
    info->value.integer.min = -90 * 0x10000;
    info->value.integer.max = 10 * 0x10000;
    info->value.integer.step = 0x08000;

    return 0;
}

static int snd_alsa_volume_get(struct snd_kcontrol *kcontrol,
    struct snd_ctl_elem_value *value)
{
    xi_card_driver *drv = snd_kcontrol_chip(kcontrol);
    int i;

    for (i = 0; i < 8; i++) {
        value->value.integer.value[i] =
                parameter_GetVolume(xi_driver_get_parameter_manager(drv->driver), drv->m_iChannel, i);
    }

    return 0;
}

static int snd_alsa_volume_put(struct snd_kcontrol *kcontrol,
    struct snd_ctl_elem_value *value)
{
    xi_card_driver *drv = snd_kcontrol_chip(kcontrol);
    int i;

    for (i = 0; i < 8; i++) {
        parameter_SetVolume(xi_driver_get_parameter_manager(drv->driver),
                            drv->m_iChannel, i, value->value.integer.value[i]);
    }

    return 0;
}

static const struct snd_kcontrol_new snd_alsa_volume = {
    .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
    .access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
    .name = "Capture Volume",
    .info = snd_alsa_volume_info,
    .get = snd_alsa_volume_get,
    .put = snd_alsa_volume_put,
};

static int snd_alsa_switch_info(struct snd_kcontrol *kcontrol,
                struct snd_ctl_elem_info *info)
{
    info->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
    info->count = 8;
    info->value.integer.min = 0;
    info->value.integer.max = 1;

    return 0;
}

static int snd_alsa_switch_get(struct snd_kcontrol *kcontrol,
    struct snd_ctl_elem_value *value)
{
    xi_card_driver *drv = snd_kcontrol_chip(kcontrol);
    int i;

    for (i = 0; i < 8; i++) {
        value->value.integer.value[i] =
                !parameter_GetMute(xi_driver_get_parameter_manager(drv->driver), drv->m_iChannel, i);
    }

    return 0;
}

static int snd_alsa_switch_put(struct snd_kcontrol *kcontrol,
    struct snd_ctl_elem_value *value)
{
    xi_card_driver *drv = snd_kcontrol_chip(kcontrol);
    int i;

    for (i = 0; i < 8; i++) {
        parameter_SetMute(xi_driver_get_parameter_manager(drv->driver),
                            drv->m_iChannel, i, !value->value.integer.value[i]);
    }

    return 0;
}

static const struct snd_kcontrol_new snd_alsa_switch = {
    .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
    .access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
    .name = "Capture Switch",
    .info = snd_alsa_switch_info,
    .get = snd_alsa_switch_get,
    .put = snd_alsa_switch_put,
};


int alsa_card_init(void *driver, int iChannel, struct snd_card **pcard, void *parent_dev)
{
    int err;
    xi_card_driver *drv;
    struct snd_card  *card;
    char driver_name[32];

    if (audio_devno >= SNDRV_CARDS)
        return (-ENODEV);

    if (!enable[audio_devno]) {
        ++audio_devno;
        return (-ENOENT);
    }

    if (xi_driver_get_family_name(driver, driver_name, sizeof(driver_name)) != 0)
        os_strlcpy(driver_name, VIDEO_CAP_DRIVER_NAME, sizeof(driver_name));

    /*
     * The function allocates snd_card instance via kzalloc
     * with the given space for the driver to use freely.
     * The allocated struct is stored in the given card_ret pointer.
     */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,15,0))
    err = snd_card_new(parent_dev, index[audio_devno], id[audio_devno], THIS_MODULE, sizeof(xi_card_driver), &card);
#elif defined(RHEL_RELEASE_CODE)
#if (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 1))
    err = snd_card_new(parent_dev, index[audio_devno], id[audio_devno], THIS_MODULE, sizeof(xi_card_driver), &card);
#else
    err = snd_card_create(index[audio_devno], id[audio_devno], THIS_MODULE, sizeof(xi_card_driver), &card);
    if (err == 0)
        card->dev = parent_dev;
#endif
#else
    err = snd_card_create(index[audio_devno], id[audio_devno], THIS_MODULE, sizeof(xi_card_driver), &card);
    if (err == 0)
        card->dev = parent_dev;
#endif
    if (err < 0)
        return err;

    drv = card->private_data;

    drv->driver = driver;
    drv->m_iChannel = iChannel;

    err = alsa_create_pcm(card, drv, driver_name);
    if (err < 0)
        goto error;

    err = snd_ctl_add(card, snd_ctl_new1(&snd_alsa_volume, drv));
    if (err < 0)
        goto error;
    err = snd_ctl_add(card, snd_ctl_new1(&snd_alsa_switch, drv));
    if (err < 0)
        goto error;

    os_strlcpy(card->driver, driver_name, sizeof(card->driver));
    if (xi_driver_get_card_name(driver, iChannel, card->shortname, sizeof(card->shortname)) <= 0)
        os_strlcpy(card->shortname, driver_name, sizeof(card->shortname));
    strcpy(card->longname, card->shortname);

    spin_lock_init(&drv->ring_lock);
    atomic_set(&drv->trigger, 0);

    drv->audio_wq = create_singlethread_workqueue("Audio work");
    if (NULL == drv->audio_wq) {
        err = -1;
        goto error;
    }
    INIT_WORK(&drv->start_work, _start_work_proc);
    INIT_WORK(&drv->stop_work, _stop_work_proc);

    err = snd_card_register(card);
    if (err < 0)
        goto free_wq;

    *pcard = card;

    audio_devno++;

    return 0;
free_wq:
    destroy_workqueue(drv->audio_wq);
error:
    snd_card_free(card);
    return err;
}

int alsa_card_release(struct snd_card *card)
{
    xi_card_driver *drv = card->private_data;

    if (NULL != drv) {
        destroy_workqueue(drv->audio_wq);
        snd_card_free(card);
    }

    return 0;
}

#ifdef CONFIG_PM
void alsa_suspend(struct snd_card *card)
{
    xi_card_driver *drv = card->private_data;
    xi_debug(5, "Enter ...\n");

    snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
    snd_pcm_suspend_all(drv->pcm);

    xi_debug(5, "Leave !\n");
}

void alsa_resume(struct snd_card *card)
{
    xi_debug(5, "Enter ...\n");

    snd_power_change_state(card, SNDRV_CTL_POWER_D0);

    xi_debug(5, "Leave !\n");
}
#endif
