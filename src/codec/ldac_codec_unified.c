/*
 * LDAC Unified Codec Implementation
 * RTP Packetization, Sequence Tracking, Timestamp Management
 */

#define LDAC_BUILD_DLL 1

#include "../../include/ldac_codec.h"
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

struct LDAC_CODEC_CTX {
    LDAC_CONFIG config;
    HANDLE_LDAC_BT h_enc;
    HANDLE_LDAC_ABR h_abr;
    ldacdec_t *p_dec;
    int is_encoder_active;
    int is_decoder_active;
    uint16_t seq_num;
    uint32_t timestamp;
    uint32_t ssrc;
};

HLDAC_CTX ldac_codec_create(const LDAC_CONFIG *p_cfg) {
    if (!p_cfg) return NULL;

    HLDAC_CTX ctx = (HLDAC_CTX)calloc(1, sizeof(struct LDAC_CODEC_CTX));
    if (!ctx) return NULL;

    ctx->config = *p_cfg;
    if (ctx->config.mtu <= 0) ctx->config.mtu = 679;
    if (ctx->config.sample_rate <= 0) ctx->config.sample_rate = LDAC_SAMPLING_FREQ_096000;
    if (ctx->config.channel_mode <= 0) ctx->config.channel_mode = LDACBT_CHANNEL_MODE_STEREO;
    if (ctx->config.eqmid < 0) ctx->config.eqmid = LDACBT_EQMID_HQ;
    if (ctx->config.sample_format <= 0) ctx->config.sample_format = LDACBT_SMPL_FMT_S24;

    ctx->seq_num = 1;
    ctx->timestamp = 0;
    ctx->ssrc = 0x534F4E59; /* 'SONY' */

    /* Initialize Encoder */
    ctx->h_enc = ldacBT_get_handle();
    if (ctx->h_enc) {
        int res = ldacBT_init_handle_encode(
            ctx->h_enc,
            ctx->config.mtu,
            ctx->config.eqmid,
            ctx->config.channel_mode,
            (LDACBT_SMPL_FMT_T)ctx->config.sample_format,
            ctx->config.sample_rate
        );
        if (res == 0) {
            ctx->is_encoder_active = 1;
        }

        /* Initialize ABR */
        ctx->h_abr = ldac_ABR_get_handle();
        if (ctx->h_abr) {
            ldac_ABR_Init(ctx->h_abr, 10 /* 10ms interval */);
        }
    }

    /* Initialize Decoder */
    ctx->p_dec = ldacdec_create();
    if (ctx->p_dec) {
        ctx->is_decoder_active = 1;
    }

    return ctx;
}

void ldac_codec_destroy(HLDAC_CTX ctx) {
    if (!ctx) return;
    if (ctx->h_enc) {
        ldacBT_free_handle(ctx->h_enc);
        ctx->h_enc = NULL;
    }
    if (ctx->h_abr) {
        ldac_ABR_free_handle(ctx->h_abr);
        ctx->h_abr = NULL;
    }
    if (ctx->p_dec) {
        ldacdec_destroy(ctx->p_dec);
        ctx->p_dec = NULL;
    }
    free(ctx);
}

int ldac_codec_set_bitrate_mode(HLDAC_CTX ctx, int eqmid) {
    if (!ctx || !ctx->h_enc) return -1;
    ctx->config.eqmid = eqmid;
    return ldacBT_set_eqmid(ctx->h_enc, eqmid);
}

int ldac_codec_get_bitrate(HLDAC_CTX ctx) {
    if (!ctx || !ctx->h_enc) return 0;
    return ldacBT_get_bitrate(ctx->h_enc);
}

int ldac_codec_encode_packet(
    HLDAC_CTX ctx,
    const void *p_pcm_interleaved,
    int pcm_samples_per_ch,
    uint8_t *p_out_packet,
    int max_out_size,
    int *p_samples_consumed,
    int *p_packet_size
) {
    (void)pcm_samples_per_ch;
    if (!ctx || !ctx->h_enc || !p_pcm_interleaved || !p_out_packet) return -1;

    /* Reserve RTP Header (12 bytes) + LDAC Header (1 byte) = 13 bytes */
    const int header_size = (int)sizeof(LDAC_RTP_HEADER);
    if (max_out_size <= header_size) return -1;

    uint8_t *p_payload = p_out_packet + header_size;
    int stream_sz = max_out_size - header_size;
    int pcm_used = 0;
    int frame_num = 0;

    int res = ldacBT_encode(
        ctx->h_enc,
        (void*)p_pcm_interleaved,
        &pcm_used,
        p_payload,
        &stream_sz,
        &frame_num
    );

    if (res < 0 || frame_num <= 0 || stream_sz <= 0) {
        if (p_samples_consumed) *p_samples_consumed = 0;
        if (p_packet_size) *p_packet_size = 0;
        return res;
    }

    /* Build RTP + LDAC Media Header */
    LDAC_RTP_HEADER *rtp = (LDAC_RTP_HEADER*)p_out_packet;
    rtp->v_p_x_cc = 0x80;        /* V=2, P=0, X=0, CC=0 */
    rtp->m_pt = 0x60;            /* M=0, PT=96 (Dynamic A2DP payload) */
    rtp->sequence_number = htons(ctx->seq_num++);
    rtp->timestamp = htonl(ctx->timestamp);
    rtp->ssrc = htonl(ctx->ssrc);
    rtp->ldac_header = (uint8_t)(frame_num & 0x07);

    /* Advance RTP timestamp by number of audio samples sent per channel */
    int samples_sent = (ctx->config.sample_rate >= 88200 ? 256 : 128) * frame_num;
    ctx->timestamp += samples_sent;

    if (p_samples_consumed) *p_samples_consumed = samples_sent;
    if (p_packet_size) *p_packet_size = header_size + stream_sz;

    return 0;
}

int ldac_codec_decode_packet(
    HLDAC_CTX ctx,
    const uint8_t *p_in_packet,
    int in_packet_size,
    void *p_out_pcm,
    int max_pcm_samples,
    int *p_samples_decoded
) {
    (void)max_pcm_samples;
    if (!ctx || !ctx->p_dec || !p_in_packet || !p_out_pcm) return -1;
    const int header_size = (int)sizeof(LDAC_RTP_HEADER);
    if (in_packet_size <= header_size) return -1;

    const LDAC_RTP_HEADER *rtp = (const LDAC_RTP_HEADER*)p_in_packet;
    int frame_count = rtp->ldac_header & 0x07;
    if (frame_count <= 0) frame_count = 1;

    const uint8_t *stream_ptr = p_in_packet + header_size;
    int remaining_bytes = in_packet_size - header_size;

    int total_samples_per_ch = 0;
    int channels = ldacdecGetChannelCount(ctx->p_dec);
    if (channels <= 0) channels = 2;

    int16_t *pcm16 = (int16_t*)p_out_pcm;
    int32_t *pcm32 = (int32_t*)p_out_pcm;
    float   *pcmF  = (float*)p_out_pcm;

    for (int f = 0; f < frame_count && remaining_bytes > 0; f++) {
        if (total_samples_per_ch + 256 > max_pcm_samples) break;
        int bytes_used = 0;
        int res = 0;

        if (ctx->config.sample_format == LDACBT_SMPL_FMT_S16) {
            res = ldacDecode(ctx->p_dec, stream_ptr, pcm16 + (total_samples_per_ch * channels), &bytes_used);
        } else if (ctx->config.sample_format == LDACBT_SMPL_FMT_S24 || ctx->config.sample_format == LDACBT_SMPL_FMT_S32) {
            res = ldacDecode24(ctx->p_dec, stream_ptr, pcm32 + (total_samples_per_ch * channels), &bytes_used);
        } else if (ctx->config.sample_format == LDACBT_SMPL_FMT_F32) {
            res = ldacDecodeFloat(ctx->p_dec, stream_ptr, pcmF + (total_samples_per_ch * channels), &bytes_used);
        } else {
            res = ldacDecode(ctx->p_dec, stream_ptr, pcm16 + (total_samples_per_ch * channels), &bytes_used);
        }

        if (res < 0 || bytes_used <= 0) break;

        int frame_samples = ldacdecGetFrameSamples(ctx->p_dec);
        total_samples_per_ch += frame_samples;
        stream_ptr += bytes_used;
        remaining_bytes -= bytes_used;
    }

    if (p_samples_decoded) *p_samples_decoded = total_samples_per_ch;
    return (total_samples_per_ch > 0) ? 0 : -1;
}
