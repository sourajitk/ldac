/*
 * Bluetooth A2DP & AVDTP Protocol Engine for Sony LDAC
 * Windows Bluetooth Audio Architecture
 */

#ifndef _A2DP_LDAC_H_
#define _A2DP_LDAC_H_

#include <stdint.h>
#include <winsock2.h>
#include <ws2bth.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard Bluetooth Protocol/Service Multiplexers (PSM) */
#define BT_PSM_SDP          0x0001
#define BT_PSM_RFCOMM       0x0003
#define BT_PSM_AVCTP        0x0017
#define BT_PSM_AVDTP        0x0019

/* AVDTP Message Types */
#define AVDTP_MSG_TYPE_COMMAND         0
#define AVDTP_MSG_TYPE_GENERAL_REJECT  1
#define AVDTP_MSG_TYPE_RESPONSE_ACCEPT 2
#define AVDTP_MSG_TYPE_RESPONSE_REJECT 3

/* AVDTP Packet Types */
#define AVDTP_PKT_TYPE_SINGLE   0
#define AVDTP_PKT_TYPE_START    1
#define AVDTP_PKT_TYPE_CONTINUE 2
#define AVDTP_PKT_TYPE_END      3

/* AVDTP Signal Identifiers */
#define AVDTP_DISCOVER          0x01
#define AVDTP_GET_CAPABILITIES  0x02
#define AVDTP_SET_CONFIGURATION 0x03
#define AVDTP_GET_CONFIGURATION 0x04
#define AVDTP_RECONFIGURE       0x05
#define AVDTP_OPEN              0x06
#define AVDTP_START             0x07
#define AVDTP_CLOSE             0x08
#define AVDTP_SUSPEND           0x09
#define AVDTP_ABORT             0x0A
#define AVDTP_SECURITY_CONTROL  0x0B
#define AVDTP_GET_ALL_CAPABILITIES 0x0C
#define AVDTP_DELAYREPORT       0x0D

/* AVDTP Service Category */
#define AVDTP_SERVICE_MEDIA_TRANSPORT    0x01
#define AVDTP_SERVICE_REPORTING          0x02
#define AVDTP_SERVICE_RECOVERY           0x03
#define AVDTP_SERVICE_CONTENT_PROTECTION 0x04
#define AVDTP_SERVICE_HEADER_COMPRESSION 0x05
#define AVDTP_SERVICE_MULTIPLEXING       0x06
#define AVDTP_SERVICE_MEDIA_CODEC        0x07
#define AVDTP_SERVICE_DELAY_REPORTING    0x08

/* A2DP Media Types */
#define A2DP_MEDIA_TYPE_AUDIO            0x00
#define A2DP_MEDIA_TYPE_VIDEO            0x01
#define A2DP_MEDIA_TYPE_MULTIMEDIA       0x02

/* A2DP Codec Types */
#define A2DP_CODEC_SBC                   0x00
#define A2DP_CODEC_MPEG12                0x01
#define A2DP_CODEC_MPEG_AAC              0x02
#define A2DP_CODEC_ATRAC                 0x04
#define A2DP_CODEC_VENDOR_SPECIFIC       0xFF

/* Sony Corporation Bluetooth Vendor ID and LDAC Codec ID */
#define SONY_VENDOR_ID                   0x0000012D  /* Sony Corporation */
#define LDAC_CODEC_ID                    0x00AA      /* LDAC */

/* LDAC AVDTP Codec Specific Information Elements */
/* Octet 0-3: Vendor ID (0x2D, 0x01, 0x00, 0x00) */
/* Octet 4-5: Codec ID (0xAA, 0x00) */

/* Octet 6: Sampling Frequency Mask */
#define A2DP_LDAC_SAMPLING_FREQ_044100   0x20
#define A2DP_LDAC_SAMPLING_FREQ_048000   0x10
#define A2DP_LDAC_SAMPLING_FREQ_088200   0x08
#define A2DP_LDAC_SAMPLING_FREQ_096000   0x04
#define A2DP_LDAC_SAMPLING_FREQ_176400   0x02
#define A2DP_LDAC_SAMPLING_FREQ_192000   0x01

/* Octet 7: Channel Mode Mask */
#define A2DP_LDAC_CHANNEL_MODE_MONO      0x04
#define A2DP_LDAC_CHANNEL_MODE_DUAL      0x02
#define A2DP_LDAC_CHANNEL_MODE_STEREO    0x01

#pragma pack(push, 1)
/* Vendor Specific Codec Information Element for LDAC */
typedef struct {
    uint8_t vendor_id[4];     /* 0x2D, 0x01, 0x00, 0x00 (0x0000012D in Little Endian) */
    uint8_t codec_id[2];      /* 0xAA, 0x00 (0x00AA in Little Endian) */
    uint8_t sample_rate_mask; /* A2DP_LDAC_SAMPLING_FREQ_* */
    uint8_t channel_mode_mask;/* A2DP_LDAC_CHANNEL_MODE_* */
} A2DP_LDAC_CODEC_CONFIG;

/* Stream Endpoint Discovery Item */
typedef struct {
    uint8_t seid;             /* Stream Endpoint ID (1 to 62) */
    uint8_t in_use;           /* 1 = in use, 0 = available */
    uint8_t media_type;       /* A2DP_MEDIA_TYPE_AUDIO */
    uint8_t tsep;             /* 0 = Source, 1 = Sink */
    uint8_t is_ldac_capable;  /* 1 if Sony LDAC capability detected */
    A2DP_LDAC_CODEC_CONFIG ldac_caps;
} AVDTP_SEP_INFO;
#pragma pack(pop)

/* Bluetooth Device Descriptor */
typedef struct {
    BTH_ADDR bth_addr;
    wchar_t  name[256];
    int      is_connected;
    int      is_remembered;
    int      is_sony_device;
} BTH_AUDIO_DEVICE;

#ifdef __cplusplus
}
#endif

#endif /* _A2DP_LDAC_H_ */
