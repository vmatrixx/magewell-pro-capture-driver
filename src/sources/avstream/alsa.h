////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////
#ifndef __ALSA_H__
#define __ALSA_H__

#include <sound/core.h>

int alsa_card_init(void *driver, int iChannel, struct snd_card **pcard, void *parent_dev);

int alsa_card_release(struct snd_card *card);

#ifdef CONFIG_PM
void alsa_suspend(struct snd_card *card);

void alsa_resume(struct snd_card *card);
#endif

#endif /* __ALSA_H__ */
