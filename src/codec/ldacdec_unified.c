/*
 * Sony LDAC Bluetooth High-Resolution Audio Decoder
 * Unified Windows Source (MSVC / Clang compatible)
 */

#ifdef _MSC_VER
#pragma warning(disable: 4244 4267 4018 4133 4047 4024 4146 4005)
#endif

#define LDAC_BUILD_DLL 1

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../../libldacdec_src/ldacdec.h"
#include "../../include/ldacdec.h"

/* Include internal components from libldacdec_src */
#include "../../libldacdec_src/log.h"
#include "../../libldacdec_src/bit_reader.h"
#include "../../libldacdec_src/bit_allocation.h"
#include "../../libldacdec_src/huffCodes.h"
#include "../../libldacdec_src/imdct.h"
#include "../../libldacdec_src/spectrum.h"
#include "../../libldacdec_src/utility.h"

/* Include C sources directly into this compilation unit */
#include "../../libldacdec_src/utility.c"
#include "../../libldacdec_src/imdct.c"
#include "../../libldacdec_src/bit_reader.c"
#include "../../libldacdec_src/huffCodes.c"
#include "../../libldacdec_src/spectrum.c"
#include "../../libldacdec_src/bit_allocation.c"

/* Modify libldacdec.c routines to support 24-bit and 32-bit float */
#include "../../libldacdec_src/libldacdec.c"

static inline int32_t Clamp24(double value) {
    if (value > 8388607.0) return 8388607;
    if (value < -8388608.0) return -8388608;
    return (int32_t)value;
}

static inline float ClampFloat(float value) {
    if (value > 1.0f) return 1.0f;
    if (value < -1.0f) return -1.0f;
    return value;
}

ldacdec_t* ldacdec_create(void) {
    ldacdec_t *p_dec = (ldacdec_t*)malloc(sizeof(ldacdec_t));
    if (p_dec) ldacdecInit(p_dec);
    return p_dec;
}

void ldacdec_destroy(ldacdec_t *p_dec) {
    if (p_dec) free(p_dec);
}

int ldacDecode24(ldacdec_t *p_dec, const uint8_t *stream, int32_t *pcm, int *bytesUsed) {
    BitReaderCxt brObject;
    BitReaderCxt *br = &brObject;
    InitBitReaderCxt(br, (uint8_t*)stream);

    frame_t *frame = &p_dec->frame;
    int ret = decodeFrame(frame, br);
    if (ret < 0) return -1;

    int out_idx = 0;
    for (int block = 0; block < gaa_block_setting_ldac[frame->channelConfigId][1]; ++block) {
        decodeBand(frame, br);
        decodeGradient(frame, br);
        calculateGradient(frame);

        for (int i = 0; i < frame->channelCount; ++i) {
            channel_t *channel = &frame->channels[i];
            decodeScaleFactors(frame, br, i);
            calculatePrecisionMask(channel);
            calculatePrecisions(channel);

            decodeSpectrum(channel, br);
            decodeSpectrumFine(channel, br);
            dequantizeSpectra(channel);
            scaleSpectrum(channel);

            RunImdct(&channel->mdct, channel->spectra, channel->pcm);
        }
        AlignPosition(br, 8);

        for (int smpl = 0; smpl < frame->frameSamples; ++smpl) {
            for (int ch = 0; ch < frame->channelCount; ++ch) {
                pcm[out_idx++] = Clamp24(frame->channels[ch].pcm[smpl] * 256.0);
            }
        }
    }
    AlignPosition(br, (frame->frameLength) * 8 + 24);

    if (bytesUsed != NULL) *bytesUsed = br->Position / 8;
    return 0;
}

int ldacDecodeFloat(ldacdec_t *p_dec, const uint8_t *stream, float *pcm, int *bytesUsed) {
    BitReaderCxt brObject;
    BitReaderCxt *br = &brObject;
    InitBitReaderCxt(br, (uint8_t*)stream);

    frame_t *frame = &p_dec->frame;
    int ret = decodeFrame(frame, br);
    if (ret < 0) return -1;

    int out_idx = 0;
    for (int block = 0; block < gaa_block_setting_ldac[frame->channelConfigId][1]; ++block) {
        decodeBand(frame, br);
        decodeGradient(frame, br);
        calculateGradient(frame);

        for (int i = 0; i < frame->channelCount; ++i) {
            channel_t *channel = &frame->channels[i];
            decodeScaleFactors(frame, br, i);
            calculatePrecisionMask(channel);
            calculatePrecisions(channel);

            decodeSpectrum(channel, br);
            decodeSpectrumFine(channel, br);
            dequantizeSpectra(channel);
            scaleSpectrum(channel);

            RunImdct(&channel->mdct, channel->spectra, channel->pcm);
        }
        AlignPosition(br, 8);

        for (int smpl = 0; smpl < frame->frameSamples; ++smpl) {
            for (int ch = 0; ch < frame->channelCount; ++ch) {
                pcm[out_idx++] = ClampFloat((float)(frame->channels[ch].pcm[smpl] / 32768.0));
            }
        }
    }
    AlignPosition(br, (frame->frameLength) * 8 + 24);

    if (bytesUsed != NULL) *bytesUsed = br->Position / 8;
    return 0;
}

int ldacdecGetFrameSamples(ldacdec_t *p_dec) {
    return p_dec->frame.frameSamples;
}

/* Compatibility shim for PipeWire / libldacBT_dec */
void* ldacBT_get_handle_decode(void) {
    return (void*)ldacdec_create();
}

void ldacBT_free_handle_decode(void *hLdacBT) {
    ldacdec_destroy((ldacdec_t*)hLdacBT);
}

int ldacBT_init_handle_decode(void *hLdacBT, int channel_mode, int sample_rate, int nshift, int var1, int var2) {
    (void)channel_mode; (void)sample_rate; (void)nshift; (void)var1; (void)var2;
    if (!hLdacBT) return -1;
    return ldacdecInit((ldacdec_t*)hLdacBT);
}

int ldacBT_decode(void *hLdacBT, const unsigned char *p_stream, void *p_pcm, int fmt, int stream_sz, int *used_bytes, int *pcm_sz) {
    if (!hLdacBT || !p_stream || !p_pcm) return -1;
    ldacdec_t *dec = (ldacdec_t*)hLdacBT;
    int bytes = 0;
    int res = 0;

    if (fmt == 0x2 /* S16 */) {
        res = ldacDecode(dec, p_stream, (int16_t*)p_pcm, &bytes);
        if (pcm_sz) *pcm_sz = dec->frame.frameSamples * dec->frame.channelCount * sizeof(int16_t);
    } else if (fmt == 0x3 /* S24 */ || fmt == 0x4 /* S32 */) {
        res = ldacDecode24(dec, p_stream, (int32_t*)p_pcm, &bytes);
        if (pcm_sz) *pcm_sz = dec->frame.frameSamples * dec->frame.channelCount * sizeof(int32_t);
    } else if (fmt == 0x5 /* F32 */) {
        res = ldacDecodeFloat(dec, p_stream, (float*)p_pcm, &bytes);
        if (pcm_sz) *pcm_sz = dec->frame.frameSamples * dec->frame.channelCount * sizeof(float);
    } else {
        res = ldacDecode(dec, p_stream, (int16_t*)p_pcm, &bytes);
        if (pcm_sz) *pcm_sz = dec->frame.frameSamples * dec->frame.channelCount * sizeof(int16_t);
    }

    if (used_bytes) *used_bytes = bytes;
    return res;
}
