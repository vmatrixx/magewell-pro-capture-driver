////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing) 
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////
#ifndef __FRONT_END_TYPES_H__
#define __FRONT_END_TYPES_H__

// Input specific status
typedef struct _INPUT_SPECIFIC_STATUS {
    MWCAP_INPUT_SPECIFIC_STATUS     pubStatus;
    WORD							wAdjustedHSyncWidth;
} INPUT_SPECIFIC_STATUS;

// Video signal status
typedef struct _VIDEO_SIGNAL_STATUS {
    MWCAP_VIDEO_SIGNAL_STATUS       pubStatus;

    BYTE							byInputSelect;

    SHORT							sRVBUScale;
    SHORT							sRVBUOffset;

    int								nEffectiveAspectX;
    int								nEffectiveAspectY;
    MWCAP_VIDEO_COLOR_FORMAT		nEffectiveColorFormat;
    MWCAP_VIDEO_QUANTIZATION_RANGE	nEffectiveQuantRange;

    BOOLEAN							bFramePacking;
    int								yRightFrame;

    BOOLEAN							bInterlacedFramePacking;
    int								cyInterlacedFramePacking;
} VIDEO_SIGNAL_STATUS;

// Audio signal status
typedef enum _AUDIO_INPUT_MODE {
    AUDIO_INPUT_SERIAL				= 0,
    AUDIO_INPUT_I2S					= 1,
    AUDIO_INPUT_DSD					= 2,
    AUDIO_INPUT_SPDIF				= 3,
    AUDIO_INPUT_INTERNAL			= 4
} AUDIO_INPUT_MODE;

typedef struct _AUDIO_SIGNAL_STATUS {
    MWCAP_AUDIO_SIGNAL_STATUS       pubStatus;
    BOOLEAN							bFreeRun;
    BYTE							byInputSelect;
    AUDIO_INPUT_MODE				audioInputMode;
    BOOLEAN							bLeftAlign;
    BOOLEAN							bMSBFirst;
    BYTE							byMSBIndex;
    BOOLEAN							bAnalogVolumeControl;
    BYTE							byChannelOffset;
} AUDIO_SIGNAL_STATUS;

#endif /* __FRONT_END_TYPES_H__ */
