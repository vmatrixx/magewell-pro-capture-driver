////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing) 
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////

#ifndef __PARAMETER_MANAGER_H__
#define __PARAMETER_MANAGER_H__

#include "supports/karray.h"
#include "supports/math.h"

#include "mw-procapture-extension-private.h"

#define MAX_CHANNELS_PER_DEVICE								8

typedef enum _PARAMETER_EDID_TYPE {
    PARAMETER_EDID_2K,
    PARAMETER_EDID_4K,
    PARAMETER_EDID_4Kp,
    PARAMETER_EDID_DVI_4K
} PARAMETER_EDID_TYPE;

// Timestamp mode
typedef enum _TIMESTAMP_MODE {
    TIMESTAMP_MODE_NORMAL,
    TIMESTAMP_MODE_NONE,
    TIMESTAMP_MODE_MAX = TIMESTAMP_MODE_NONE
} TIMESTAMP_MODE;

struct parameter_t {
    void                           *driver;

    PARAMETER_EDID_TYPE             m_nEDIDType;

    DWORD							m_dwLEDMode;

    // Fan
    BYTE							m_byFanDuty;

    // Preview
    BOOLEAN							m_bEnablePreview;

    // XBar
    BOOLEAN							m_bEnableXBar;

    BOOLEAN							m_bTopFieldFirst;

    // HDCP
    BOOLEAN							m_bEnableHDCP;

    // EDID
    BYTE							m_byEDIDMode;
    BYTE							m_abyEDID[256];
    WORD							m_cbEDID;

    // Signal detection
    BOOLEAN							m_bEnableHPSyncMeter;
    DWORD							m_dwFrameDurationChangeThreshold;

    DWORD							m_dwVideoSyncSliceLevel;

    // Auto scan signal sources
    BOOLEAN							m_bInputSourceScan;
    DWORD							m_dwScanPeriod;
    DWORD                           m_dwScanKeepDuration;

    struct karray                   m_arrScanInputSources; //DWORD

    BOOLEAN							m_bAVInputSourceLink;

    DWORD							m_dwVideoInputSource;
    DWORD							m_dwAudioInputSource;

    // Video timings
    BOOLEAN                         m_bAutoHAlign;

    BYTE                            m_bySamplingPhase;
    BOOLEAN                         m_bAutoSamplingPhaseAdjust;

        /* MWCAP_VIDEO_CUSTOM_TIMING */
    struct karray                   m_arrCustomVideoTimings;

    // Video resolutions
        /* MWCAP_SIZE */
    struct karray                   m_arrVideoResolutions;

    // Video activity detection threshold
    BYTE							m_byVADThreshold;

    // Audio controls
    BOOLEAN							m_abMute[8 * MAX_CHANNELS_PER_DEVICE];
    int                             m_anVolume[8 * MAX_CHANNELS_PER_DEVICE];
    int								m_nSampleRateAdjustment;

    // Timestamp mode
    TIMESTAMP_MODE                  m_nTimestampMode;
    DWORD							m_dwTimestampOffsetVideo;
	DWORD							m_dwTimestampOffsetAudio;


    // Video controls
    int								m_nVideoInputAspectX;
    int								m_nVideoInputAspectY;
    MWCAP_VIDEO_COLOR_FORMAT		m_colorFormatVideoInput;
    MWCAP_VIDEO_QUANTIZATION_RANGE	m_quantRangeVideoInput;

    int								m_nLowLatencyStripeHeight;

    BOOLEAN                         m_bQuadSDIForceConvMode2SI;
};

int parameter_manager_init(struct parameter_t *par, void *driver, PARAMETER_EDID_TYPE nEDIDType);

void parameter_manager_deinit(struct parameter_t *par);

static inline BYTE parameter_GetFanDuty(struct parameter_t *par)
{
    return par->m_byFanDuty;
}

static inline DWORD parameter_GetLEDMode(struct parameter_t *par)
{
    return par->m_dwLEDMode;
}

// Preview
static inline BOOLEAN parameter_GetEnablePreview(struct parameter_t *par)
{
    return par->m_bEnablePreview;
}

// Crossbar
static inline BOOLEAN parameter_GetEnableXBar(struct parameter_t *par)
{
    return par->m_bEnableXBar;
}

static inline void parameter_SetEnableXBar(struct parameter_t *par, BOOLEAN bEnableXBar)
{
    par->m_bEnableXBar = !!bEnableXBar;
}


static inline BOOLEAN parameter_GetTopFieldFirst(struct parameter_t *par)
{
    return par->m_bTopFieldFirst;
}
static inline void parameter_SetTopFieldFirst(struct parameter_t *par,
                                                 bool bTopFieldFirst)
{
    par->m_bTopFieldFirst = !!bTopFieldFirst;
}

// HDCP
static inline BOOLEAN parameter_GetEnableHDCP(struct parameter_t *par)
{
    return par->m_bEnableHDCP;
}

static inline void parameter_SetEnableHDCP(struct parameter_t *par, BOOLEAN bEnableHDCP)
{
    par->m_bEnableHDCP = !!bEnableHDCP;
}

// EDID
void parameter_SetEDID(struct parameter_t *par, const BYTE * pbyEDID, WORD cbEDID);

void parameter_ResetEDID(struct parameter_t *par);

static inline const BYTE * parameter_GetEDID(struct parameter_t *par)
{
    return par->m_abyEDID;
}

static inline WORD parameter_GetEDIDSize(struct parameter_t *par)
{
    return par->m_cbEDID;
}

static inline BYTE parameter_GetEDIDMode(struct parameter_t *par)
{
    return par->m_byEDIDMode;
}

static inline void parameter_SetEDIDMode(struct parameter_t *par, BYTE byEDIDMode)
{
    par->m_byEDIDMode = byEDIDMode;
}

// Signal detection
static inline void parameter_SetEnableHPSyncMeter(struct parameter_t *par, BOOLEAN bEnableHPSyncMeter)
{
    par->m_bEnableHPSyncMeter = !!bEnableHPSyncMeter;
}

static inline BOOLEAN parameter_GetEnableHPSyncMeter(struct parameter_t *par)
{
    return par->m_bEnableHPSyncMeter;
}

static inline void parameter_SetFrameDurationChangeThreshold(struct parameter_t *par,
        DWORD dwFrameDurationChangeThreshold)
{
    par->m_dwFrameDurationChangeThreshold =
            _limit(dwFrameDurationChangeThreshold, 0, 10000) ;
}

static inline DWORD parameter_GetFrameDurationChangeThreshold(struct parameter_t *par)
{
    return par->m_dwFrameDurationChangeThreshold;
}

// Video sync slice level
static inline DWORD parameter_GetVideoSyncSliceLevel(struct parameter_t *par)
{
    return par->m_dwVideoSyncSliceLevel;
}

static inline void parameter_SetVideoSyncSliceLevel(
        struct parameter_t *par, DWORD dwVideoSyncSliceLevel)
{
    par->m_dwVideoSyncSliceLevel = _limit(dwVideoSyncSliceLevel, 300, 600);
}

// Auto scan signal source
static inline void parameter_SetInputSourceScan(struct parameter_t *par, BOOLEAN bInputSourceScan)
{
    par->m_bInputSourceScan = !!bInputSourceScan;
}

static inline BOOLEAN parameter_GetInputSourceScan(struct parameter_t *par)
{
    return par->m_bInputSourceScan;
}

static inline void parameter_SetScanPeriod(struct parameter_t *par, DWORD dwScanPeriod)
{
    par->m_dwScanPeriod = _limit(dwScanPeriod, 100, 1000);
}

static inline DWORD parameter_GetScanPeriod(struct parameter_t *par)
{
    return par->m_dwScanPeriod;
}

static inline void parameter_SetScanKeepDuration(struct parameter_t *par,
        DWORD dwScanKeepDuration)
{
    par->m_dwScanKeepDuration = _limit(dwScanKeepDuration, 100, 1000);
}

static inline DWORD parameter_GetScanKeepDuration(struct parameter_t *par)
{
    return par->m_dwScanKeepDuration;
}

BOOLEAN parameter_SetScanInputSources(struct parameter_t *par,
        const DWORD * pdwScanInputSources, int cScanInputSources);

const DWORD *parameter_GetScanInputSources(struct parameter_t *par);

int parameter_GetScanInputSourceCount(struct parameter_t *par);

static inline void parameter_SetAVInputSourceLink(struct parameter_t *par,
        BOOLEAN bAVInputSourceLink)
{
    par->m_bAVInputSourceLink = !!bAVInputSourceLink;
}

static inline BOOLEAN parameter_GetAVInputSourceLink(struct parameter_t *par)
{
    return par->m_bAVInputSourceLink;
}

static inline void parameter_SetVideoInputSource(struct parameter_t *par,
        DWORD dwVideoInputSource)
{
    par->m_dwVideoInputSource = dwVideoInputSource;
}

static inline DWORD parameter_GetVideoInputSource(struct parameter_t *par)
{
    return par->m_dwVideoInputSource;
}

static inline void parameter_SetAudioInputSource(struct parameter_t *par,
        DWORD dwAudioInputSource)
{
    par->m_dwAudioInputSource = dwAudioInputSource;
}

static inline DWORD parameter_GetAudioInputSource(struct parameter_t *par)
{
    return par->m_dwAudioInputSource;
}

// Video timings
BOOLEAN parameter_SetCustomVideoTimings(struct parameter_t *par,
        const MWCAP_VIDEO_CUSTOM_TIMING *pVideoTimings, int cVideoTimings);

const MWCAP_VIDEO_CUSTOM_TIMING *parameter_GetCustomVideoTimings(struct parameter_t *par);

int parameter_GetCustomVideoTimingCount(struct parameter_t *par);

// Resolutions
BOOLEAN parameter_SetVideoResolutions(struct parameter_t *par,
        const MWCAP_SIZE * pVideoResolutions, int cVideoResolutions);

const MWCAP_SIZE *parameter_GetVideoResolutions(struct parameter_t *par);

int parameter_GetVideoResolutionCount(struct parameter_t *par);

// Video activity detection threshold
static inline BYTE parameter_GetVADThreshold(struct parameter_t *par)
{
    return par->m_byVADThreshold;
}

static inline void parameter_SetVADThreshold(struct parameter_t *par,
        BYTE byVADThreshold)
{
    par->m_byVADThreshold = _limit(byVADThreshold, 0, 255);
}

static inline int parameter_GetCPPosAdjustLimit(struct parameter_t *par)
{
    return 64;
}

// Audio controls
static inline BOOLEAN parameter_GetMute(struct parameter_t *par, int i, LONG lChannel)
{
    if (lChannel >= ARRAY_SIZE(par->m_abMute) || lChannel < 0)
        return FALSE;

    return par->m_abMute[i * 8 + lChannel];
}

static inline int parameter_GetVolume(struct parameter_t *par, int i, int nChannel)
{
    if (nChannel >= ARRAY_SIZE(par->m_anVolume) || nChannel < 0)
        return 0;

    return par->m_anVolume[i * 8 + nChannel];
}

static inline void parameter_SetMute(struct parameter_t *par, int i, LONG lChannel, BOOLEAN bMute)
{
    if (lChannel >= ARRAY_SIZE(par->m_abMute) || lChannel < 0)
        return;

    par->m_abMute[i * 8 + lChannel] = !!bMute;
}

static inline void parameter_SetVolume(struct parameter_t *par, int i, int nChannel, int nVolume)
{
    if (nChannel >= ARRAY_SIZE(par->m_anVolume) || nChannel < 0)
        return;

    par->m_anVolume[i * 8 + nChannel] = _limit(nVolume, -90 * 0x10000, 10 * 0x10000);
}

static inline int parameter_GetSampleRateAdjustment(struct parameter_t *par)
{
    return par->m_nSampleRateAdjustment;
}

// Input parameters
static inline BOOLEAN parameter_GetVideoInputAspectRatio(struct parameter_t *par,
        int *nInputAspectX, int *nInputAspectY)
{
    if (nInputAspectX != NULL)
        *nInputAspectX = par->m_nVideoInputAspectX;
    if (nInputAspectY != NULL)
        *nInputAspectY = par->m_nVideoInputAspectY;
    return (par->m_nVideoInputAspectX > 0 && par->m_nVideoInputAspectY > 0);
}

static inline void parameter_SetVideoInputAspectRatio(struct parameter_t *par,
        int nInputAspectX, int nInputAspectY)
{
    par->m_nVideoInputAspectX = _limit(nInputAspectX, 0, 2048);
    par->m_nVideoInputAspectY = _limit(nInputAspectY, 0, 2160);
}

static inline MWCAP_VIDEO_COLOR_FORMAT parameter_GetVideoInputColorFormat(struct parameter_t *par)
{
    return par->m_colorFormatVideoInput;
}

static inline void parameter_SetVideoInputColorFormat(struct parameter_t *par,
        MWCAP_VIDEO_COLOR_FORMAT colorFormat)
{
    par->m_colorFormatVideoInput =
            _limit(colorFormat, MWCAP_VIDEO_COLOR_FORMAT_UNKNOWN,
                   MWCAP_VIDEO_COLOR_FORMAT_YUV2020);
}

static inline MWCAP_VIDEO_QUANTIZATION_RANGE
parameter_GetVideoInputQuantizationRange(struct parameter_t *par)
{
    return par->m_quantRangeVideoInput;
}

static inline void parameter_SetVideoInputQuantizationRange(
        struct parameter_t *par, MWCAP_VIDEO_QUANTIZATION_RANGE quantRange)
{
    par->m_quantRangeVideoInput =
            _limit(quantRange, MWCAP_VIDEO_QUANTIZATION_UNKNOWN,
                   MWCAP_VIDEO_QUANTIZATION_LIMITED);
}

// Timings
static inline BOOLEAN parameter_GetAutoSamplingPhaseAdjust(struct parameter_t *par)
{
    return par->m_bAutoSamplingPhaseAdjust;
}

static inline void parameter_SetAutoSamplingPhaseAdjust(struct parameter_t *par,
    BOOLEAN bAutoSamplingPhaseAdjust)
{
    par->m_bAutoSamplingPhaseAdjust = !!bAutoSamplingPhaseAdjust;
}

static inline void parameter_SetAutoHAlign(struct parameter_t *par, BOOLEAN bAutoHAlign)
{
    par->m_bAutoHAlign = !!bAutoHAlign;
}

static inline BOOLEAN parameter_GetAutoHAlign(struct parameter_t *par)
{
    return par->m_bAutoHAlign;
}

static inline BYTE parameter_GetSamplingPhase(struct parameter_t *par)
{
    return par->m_bySamplingPhase;
}

static inline void parameter_SetSamplingPhase(struct parameter_t *par, BYTE bySamplingPhase)
{
    par->m_bySamplingPhase = _limit(bySamplingPhase, 0, 63);
}

static inline int parameter_GetLowLatencyStripeHeight(struct parameter_t *par)
{
    return par->m_nLowLatencyStripeHeight;
}

static inline void parameter_SetLowLatencyStripeHeight(struct parameter_t *par,
                                                      int nLowLatencyStripeHeight)
{
    par->m_nLowLatencyStripeHeight = _limit(nLowLatencyStripeHeight, 32, 2160);
}

static inline TIMESTAMP_MODE parameter_GetTimestampMode(struct parameter_t *par)
{
    return par->m_nTimestampMode;
}

static inline void parameter_SetTimestampMode(struct parameter_t *par,
                                                        TIMESTAMP_MODE mode)
{
    par->m_nTimestampMode = _limit(mode, TIMESTAMP_MODE_NORMAL, TIMESTAMP_MODE_MAX);
}

// Timestamp offset
static inline DWORD GetTimestampOffsetVideo(struct parameter_t *par
    )
{
    return par->m_dwTimestampOffsetVideo * 10000;
}

static inline DWORD GetTimestampOffsetAudio(struct parameter_t *par
)
{
    return par->m_dwTimestampOffsetAudio * 10000;
}

// QuadSDIForceConvMode2SI is use for fixing QuadLink signal in china TV.
static inline BOOLEAN parameter_GetQuadSDIForceConvMode2SI(struct parameter_t *par)
{
    return par->m_bQuadSDIForceConvMode2SI;
}

static inline void parameter_SetQuadSDIForceConvMode2SI(
        struct parameter_t *par, BOOLEAN bQuadSDIForceConvMode2SI)
{
    par->m_bQuadSDIForceConvMode2SI = bQuadSDIForceConvMode2SI;
}

#endif /* __PARAMETER_MANAGER_H__ */
