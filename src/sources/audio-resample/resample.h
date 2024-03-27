/*
 * Fixed-point sampling rate conversion library
 * Developed by Ken Cooke (kenc@real.com)
 * May 2003
 *
 * Public interface.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void *InitResampler(int inrate, int outrate, int chans, int quality);

int Resample(short *inbuf, int insamps, short *outbuf, void *inst);

void FreeResampler(void *inst);

int GetMaxOutput(int insamps, void *inst);

int GetMinInput(int outsamps, void *inst);

int GetDelay(void *inst);

#ifdef __cplusplus
}
#endif
