////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing) 
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////
#ifndef __XI_DRIVER_H__
#define __XI_DRIVER_H__

#include "mw-procapture-extension-private.h"
#include "mw-fourcc.h"
#include "supports/xi-timer.h"
#include "supports/xi-notify-event.h"
#include "supports/image-buffer.h"

#include "front-end/front-end-types.h"

#include "picopng/picopng.h"

#include "parameter-manager.h"

#pragma pack(push)
#pragma pack(1)

typedef struct _AUDIO_CAPTURE_FRAME {
    DWORD											dwSyncCode;
    DWORD											dwReserved;
    LONGLONG										llTimestamp;
    DWORD											adwSamples[MWCAP_AUDIO_SAMPLES_PER_FRAME * MWCAP_AUDIO_MAX_NUM_CHANNELS];
} AUDIO_CAPTURE_FRAME;

#pragma pack(pop)

enum AUDIO_SIZE {
    AUDIO_FRAME_COUNT			= 32
};

typedef void (*LPFN_PARTIAL_NOTIFY)(
        MWCAP_PTR pvNotifyContext,
        BOOLEAN bFrameCompleted,
        int cyCompleted
        );

struct xi_driver {
    void                       *priv; // kmalloc, if is NULL xi_driver not init;
};

bool xi_driver_is_support_msi(volatile void __iomem *reg_base);

int xi_driver_init(struct xi_driver *pobj, volatile void __iomem *reg_base, os_dma_par_t dma_priv,
                   png_pix_info_t *nosignal_png,
                   png_pix_info_t *unsupported_png,
                   png_pix_info_t *locking_png,
                   os_pci_dev_t pci_dev,
                   os_pci_dev_t par_dev
                   );

void xi_driver_deinit(struct xi_driver *pobj);

int xi_driver_get_family_name(struct xi_driver *pobj, char *name, size_t size);
int xi_driver_get_card_name(struct xi_driver *pobj, int iChannel, void *name, size_t size);

void xi_driver_resume(struct xi_driver *pobj,
                      png_pix_info_t *nosignal_png,
                      png_pix_info_t *unsupported_png,
                      png_pix_info_t *locking_png,
                      os_pci_dev_t pci_dev,
                      os_pci_dev_t par_dev);

void xi_driver_sleep(struct xi_driver *pobj);

void xi_driver_shutdown(struct xi_driver *pobj);

struct parameter_t *xi_driver_get_parameter_manager(struct xi_driver *pobj);

u32 xi_driver_get_ref_clock_freq(struct xi_driver *pobj);

int xi_driver_get_channle_count(struct xi_driver *pobj);


/************** timestamp ***************/
long long xi_driver_get_time(struct xi_driver *pobj);

void xi_driver_set_time(struct xi_driver *pobj, long long lltime);

void xi_driver_update_time(struct xi_driver *pobj, long long lltime);

xi_timer *xi_driver_new_timer(struct xi_driver *pobj, os_event_t event);

void xi_driver_delete_timer(struct xi_driver *pobj, xi_timer *ptimer);

bool xi_driver_schedual_timer(struct xi_driver *pobj, long long llexpire, xi_timer *ptimer);

bool xi_driver_cancel_timer(struct xi_driver *pobj, xi_timer *ptimer);


/**************** notify *****************/
xi_notify_event *xi_driver_new_notify_event(
        struct xi_driver *pobj,
        int iChannel,
        u64 enable_bits,
        os_event_t event
        );

void xi_driver_delete_notify_event(struct xi_driver *pobj, int iChannel, xi_notify_event *event);

void xi_driver_notify_event_set_enable_bits(struct xi_driver *pobj, int iChannel, xi_notify_event *event, u64 enable_bits);

u64 xi_driver_notify_event_get_status_bits(struct xi_driver *pobj, int iChannel, xi_notify_event *event);

/* signal */
void xi_driver_set_input_source_scan(struct xi_driver *pobj, bool enable_scan);

bool xi_driver_get_input_source_scan_state(struct xi_driver *pobj);

bool xi_driver_get_input_source_scan(struct xi_driver *pobj);

void xi_driver_set_av_input_source_link(struct xi_driver *pobj, bool link);

bool xi_driver_get_av_input_source_link(struct xi_driver *pobj);

void xi_driver_set_video_input_source(struct xi_driver *pobj, u32 input_source);

u32 xi_driver_get_video_input_source(struct xi_driver *pobj);

void xi_driver_set_audio_input_source(struct xi_driver *pobj, u32 input_source);

u32 xi_driver_get_audio_input_source(struct xi_driver *pobj);

const u32 *xi_driver_get_supported_video_input_sources(struct xi_driver *pobj, int *count);

const u32 *xi_driver_get_supported_audio_input_sources(struct xi_driver *pobj, int *count);

void xi_driver_get_input_specific_status(struct xi_driver *pobj, int iChannel, INPUT_SPECIFIC_STATUS *status);

void xi_driver_get_video_signal_status(struct xi_driver *pobj,int iChannel,  VIDEO_SIGNAL_STATUS *status);

void xi_driver_get_audio_signal_status(struct xi_driver *pobj, int iChannel, AUDIO_SIGNAL_STATUS *status);

/* @ret: 0 OK, call bottom; <0 Stop.  */
int xi_driver_irq_process_top(struct xi_driver *pobj);

void xi_driver_irq_process_bottom(struct xi_driver *pobj);


/* video */
int xi_driver_sg_get_frame(
        struct xi_driver *pobj,
        int iChannel,
        mw_scatterlist_t *sgbuf,
        u32 sgnum,
        u32 stride,
        int i_frame,
        bool show_status,
        struct xi_image_buffer *osd_image,
        const RECT *osd_rects,
        int osd_count,
        u32 fourcc,
        int32_t cx,
        int32_t cy,
        int bpp,
        bool bottom_up,
        const RECT *src_rect,
        const RECT *target_rect,
        int aspectx,
        int aspecty,
        MWCAP_VIDEO_COLOR_FORMAT color_format,
        MWCAP_VIDEO_QUANTIZATION_RANGE quant_range,
        MWCAP_VIDEO_SATURATION_RANGE sat_range,
        short contrast,
        short brightness,
        short saturation,
        short hue,
        MWCAP_VIDEO_DEINTERLACE_MODE deinterlace_mode,
        MWCAP_VIDEO_ASPECT_RATIO_CONVERT_MODE aspect_ratio_convert_mode,
        LONG cyPartialNotify,
        LPFN_PARTIAL_NOTIFY lpfnPartialNotify,
        unsigned long pvPartialNotifyContext,
        u32 timeout/* ms */
        );

int xi_driver_sg_put_frame(
        struct xi_driver *pobj,
        u32 dest_addr,
        u32 dest_stride,
        u32 dest_x,
        u32 dest_y,
        u32 dest_width,
        u32 dest_height,
        MWCAP_VIDEO_COLOR_FORMAT cf_dest,
        MWCAP_VIDEO_QUANTIZATION_RANGE quant_dest,
        MWCAP_VIDEO_SATURATION_RANGE sat_dest,
        mw_scatterlist_t *sgbuf,
        u32 sgnum,
        bool src_bottomup,
        u32 src_stride,
        u32 src_width,
        u32 src_height,
        bool src_pixel_alpha,
        bool src_pixel_xbgr,
        u32 timeout/* ms */
        );

/******************* audio process *********************/
int xi_driver_get_completed_audio_block_id(struct xi_driver *pobj);

int xi_driver_acquire_audio_capture(struct xi_driver *pobj);

void xi_driver_release_audio_capture(struct xi_driver *pobj);

const AUDIO_CAPTURE_FRAME *xi_driver_get_audio_capture_frame(
        struct xi_driver *pobj, int block_id);

void xi_driver_normalize_audio_capture_frame(
        struct xi_driver *pobj,
        const AUDIO_SIGNAL_STATUS * pStatus,
        const AUDIO_CAPTURE_FRAME * pAudioFrameSrc,
        MWCAP_AUDIO_CAPTURE_FRAME * pAudioFrameDest
        );

void xi_driver_normalize_audio_capture_frame_mux(
        struct xi_driver *pobj,
        int iChannel,
        const AUDIO_SIGNAL_STATUS * pStatus,
        const AUDIO_CAPTURE_FRAME * pAudioFrameSrc1,
        const AUDIO_CAPTURE_FRAME * pAudioFrameSrc2,
        MWCAP_AUDIO_CAPTURE_FRAME * pAudioFrameDest
        );

int xi_driver_get_normalized_audio_capture_frame(struct xi_driver *pobj,
        int iChannel, int *block_id, MWCAP_AUDIO_CAPTURE_FRAME *audio_frame_dest);

void xi_driver_SetFrontEndVolume(
        struct xi_driver *pobj,
        int i,
        int iChannel,
        int nVolume
        );
int xi_driver_GetFrontEndVolume(
        struct xi_driver *pobj,
        int i,
        int iChannel
        );


/* Magewell Capture Extensions */
int xi_driver_get_channel_info(struct xi_driver *pobj, int iChannel, MWCAP_CHANNEL_INFO *pInfo);
void xi_driver_get_family_info(struct xi_driver *pobj, MWCAP_PRO_CAPTURE_INFO * pInfo);
void xi_driver_get_video_caps(struct xi_driver *pobj, MWCAP_VIDEO_CAPS *pInfo);
void xi_driver_get_audio_caps(struct xi_driver *pobj, MWCAP_AUDIO_CAPS *pInfo);

void xi_driver_get_video_frame_info(struct xi_driver *pobj,
                                    int iChannel,
                                    int iframe,
                                    MWCAP_VIDEO_FRAME_INFO *info);
void xi_driver_get_video_buffer_info(struct xi_driver *pobj,
                                     int iChannel,
                                     MWCAP_VIDEO_BUFFER_INFO *info);
int xi_driver_get_sdianc_packets(struct xi_driver *pobj,
                                 int iChannel,
                                 int iFrame,
                                 int iCount,
                                 MWCAP_SDI_ANC_PACKET *pPackets);

#endif /* __XI_DRIVER_H__ */
