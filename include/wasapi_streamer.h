/*
 * WASAPI High-Resolution Audio Loopback & Render Engine
 * Captures Windows System Audio at up to 96kHz / 24-bit
 */

#ifndef _WASAPI_STREAMER_H_
#define _WASAPI_STREAMER_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*audio_capture_cb)(const void *p_pcm, int samples_per_ch, int channels, int sample_rate, int bits_per_sample, void *user_data);

typedef struct WASAPI_CAPTURE_ENGINE* HWASAPI_CAPTURE;
typedef struct WASAPI_RENDER_ENGINE*  HWASAPI_RENDER;

/* Loopback Capture Engine */
HWASAPI_CAPTURE wasapi_capture_create(int target_sample_rate, int target_channels, int target_bit_depth);
int             wasapi_capture_start(HWASAPI_CAPTURE hCap, audio_capture_cb callback, void *user_data);
void            wasapi_capture_stop(HWASAPI_CAPTURE hCap);
void            wasapi_capture_destroy(HWASAPI_CAPTURE hCap);
int             wasapi_capture_get_format(HWASAPI_CAPTURE hCap, int *sample_rate, int *channels, int *bit_depth);
void            wasapi_capture_get_levels(HWASAPI_CAPTURE hCap, float *peak_left, float *peak_right);

/* Render Engine (for LDAC Receiver / Sink mode) */
HWASAPI_RENDER  wasapi_render_create(int sample_rate, int channels, int bit_depth);
int             wasapi_render_start(HWASAPI_RENDER hRen);
int             wasapi_render_write(HWASAPI_RENDER hRen, const void *p_pcm, int samples_per_ch);
void            wasapi_render_stop(HWASAPI_RENDER hRen);
void            wasapi_render_destroy(HWASAPI_RENDER hRen);

#ifdef __cplusplus
}
#endif

#endif /* _WASAPI_STREAMER_H_ */
