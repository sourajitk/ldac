/*
 * LDAC Unified Codec API (Encoder & Decoder)
 * High-Resolution Bluetooth Audio Architecture for Windows
 */

#ifndef _LDAC_CODEC_H_
#define _LDAC_CODEC_H_

#include <stdint.h>
#include <stddef.h>
#include "ldacBT.h"
#include "ldacBT_abr.h"
#include "ldacdec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LDAC Sampling Frequencies */
#define LDAC_SAMPLING_FREQ_044100   44100
#define LDAC_SAMPLING_FREQ_048000   48000
#define LDAC_SAMPLING_FREQ_088200   88200
#define LDAC_SAMPLING_FREQ_096000   96000

/* LDAC Bitrates */
#define LDAC_BITRATE_HQ_990         990000   /* 990 kbps (48kHz/96kHz) or 909 kbps (44.1kHz/88.2kHz) */
#define LDAC_BITRATE_SQ_660         660000   /* 660 kbps (48kHz/96kHz) or 606 kbps (44.1kHz/88.2kHz) */
#define LDAC_BITRATE_MQ_330         330000   /* 330 kbps (48kHz/96kHz) or 303 kbps (44.1kHz/88.2kHz) */

/* LDAC Samples per frame per channel */
#define LDAC_SAMPLES_PER_FRAME_48K  128
#define LDAC_SAMPLES_PER_FRAME_96K  256

/* High-level Packet Format */
#pragma pack(push, 1)
typedef struct {
    uint8_t  v_p_x_cc;       /* V=2, P=0, X=0, CC=0 (0x80) */
    uint8_t  m_pt;           /* M=0, PT=96 Dynamic A2DP (0x60) */
    uint16_t sequence_number;/* Network byte order */
    uint32_t timestamp;      /* Network byte order */
    uint32_t ssrc;           /* SSRC identifier */
    uint8_t  ldac_header;    /* Lower 3 bits: number of LDAC frames in this packet */
} LDAC_RTP_HEADER;
#pragma pack(pop)

/* Codec Context Handle */
typedef struct LDAC_CODEC_CTX* HLDAC_CTX;

typedef struct {
    int sample_rate;       /* 44100, 48000, 88200, 96000 */
    int channel_mode;      /* LDACBT_CHANNEL_MODE_STEREO, DUAL, MONO */
    int eqmid;             /* LDACBT_EQMID_HQ, SQ, MQ, ABR */
    int sample_format;     /* LDACBT_SMPL_FMT_S16, S24, S32, F32 */
    int mtu;               /* Bluetooth L2CAP MTU (typically 679 to 1021) */
} LDAC_CONFIG;

/* Codec Engine Management */
LDAC_API HLDAC_CTX ldac_codec_create(const LDAC_CONFIG *p_cfg);
LDAC_API void      ldac_codec_destroy(HLDAC_CTX ctx);
LDAC_API int       ldac_codec_set_bitrate_mode(HLDAC_CTX ctx, int eqmid);
LDAC_API int       ldac_codec_get_bitrate(HLDAC_CTX ctx);

/* High-level Encoder: Takes PCM samples, returns formatted A2DP RTP packet */
LDAC_API int ldac_codec_encode_packet(
    HLDAC_CTX ctx,
    const void *p_pcm_interleaved,
    int pcm_samples_per_ch,
    uint8_t *p_out_packet,
    int max_out_size,
    int *p_samples_consumed,
    int *p_packet_size
);

/* High-level Decoder: Takes A2DP RTP packet, decodes into interleaved PCM */
LDAC_API int ldac_codec_decode_packet(
    HLDAC_CTX ctx,
    const uint8_t *p_in_packet,
    int in_packet_size,
    void *p_out_pcm,
    int max_pcm_samples,
    int *p_samples_decoded
);

#ifdef __cplusplus
}
#endif

#endif /* _LDAC_CODEC_H_ */
