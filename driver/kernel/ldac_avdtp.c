/*
 * Sony LDAC Bluetooth Audio KMDF Upper Filter Driver
 * AVDTP Signaling Inspection and Sony LDAC Stream Endpoint (SEP) Injection
 * Copyright (c) 2026 LDAC-Windows Open Source Project. All rights reserved.
 */

#include "ldac_filter.h"

/* AVDTP Signal Identifiers */
#define AVDTP_SIG_DISCOVER          0x01
#define AVDTP_SIG_GET_CAPABILITIES  0x02
#define AVDTP_SIG_SET_CONFIGURATION 0x03
#define AVDTP_SIG_GET_CONFIGURATION 0x04
#define AVDTP_SIG_RECONFIGURE       0x05
#define AVDTP_SIG_OPEN              0x06
#define AVDTP_SIG_START             0x07
#define AVDTP_SIG_CLOSE             0x08
#define AVDTP_SIG_SUSPEND           0x09
#define AVDTP_SIG_ABORT             0x0A
#define AVDTP_SIG_SECURITY_CONTROL  0x0B
#define AVDTP_SIG_GET_ALL_CAPS      0x0C

/* AVDTP Service Categories */
#define AVDTP_SERVICE_MEDIA_TRANSPORT   0x01
#define AVDTP_SERVICE_REPORTING         0x02
#define AVDTP_SERVICE_RECOVERY          0x03
#define AVDTP_SERVICE_CONTENT_PROTECT   0x04
#define AVDTP_SERVICE_HEADER_COMPRESS   0x05
#define AVDTP_SERVICE_MULTIPLEXING      0x06
#define AVDTP_SERVICE_MEDIA_CODEC       0x07
#define AVDTP_SERVICE_DELAY_REPORTING   0x08

#pragma pack(push, 1)
typedef struct _AVDTP_HEADER {
    UCHAR MsgTypeAndPktType; /* [7:4] TransLabel, [3:2] PktType, [1:0] MsgType */
    UCHAR SignalId;          /* [5:0] Signal Identifier */
} AVDTP_HEADER, *PAVDTP_HEADER;

typedef struct _SONY_LDAC_CAPABILITY_ELEMENT {
    UCHAR ServiceCategory;   /* 0x07 = Media Codec */
    UCHAR LengthOfLOSC;      /* 0x0A = 10 bytes */
    UCHAR MediaType;         /* 0x00 = Audio */
    UCHAR MediaCodecType;    /* 0xFF = Vendor Specific */
    ULONG VendorId;          /* 0x0000012D = Sony Corp (Little-Endian: 2D 01 00 00) */
    USHORT CodecId;          /* 0x00AA = LDAC (Little-Endian: AA 00) */
    UCHAR SampleFreq;        /* 0x04 = 96kHz, 0x08 = 88.2kHz, 0x10 = 48kHz, 0x20 = 44.1kHz */
    UCHAR ChannelMode;       /* 0x01 = Stereo, 0x02 = Dual Channel, 0x04 = Mono */
} SONY_LDAC_CAPABILITY_ELEMENT, *PSONY_LDAC_CAPABILITY_ELEMENT;
#pragma pack(pop)

/*
 * LdacInjectSonyCapabilities: Appends or overrides codec capability tables with Sony LDAC
 */
NTSTATUS LdacInjectSonyCapabilities(
    _In_ PFILTER_DEVICE_CONTEXT DevCtx,
    _Inout_ PUCHAR Buffer,
    _In_ ULONG BufferLength
) {
    if (!Buffer || BufferLength < sizeof(SONY_LDAC_CAPABILITY_ELEMENT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    PSONY_LDAC_CAPABILITY_ELEMENT ldacCap = (PSONY_LDAC_CAPABILITY_ELEMENT)Buffer;
    ldacCap->ServiceCategory = AVDTP_SERVICE_MEDIA_CODEC;
    ldacCap->LengthOfLOSC = 10;
    ldacCap->MediaType = 0x00;        /* Audio */
    ldacCap->MediaCodecType = 0xFF;   /* Vendor Specific */
    ldacCap->VendorId = 0x0000012D;   /* Sony Corporation */
    ldacCap->CodecId = 0x00AA;        /* LDAC */
    ldacCap->SampleFreq = LDAC_SAMPLING_FREQ_96000 | LDAC_SAMPLING_FREQ_88200 |
                          LDAC_SAMPLING_FREQ_48000 | LDAC_SAMPLING_FREQ_44100;
    ldacCap->ChannelMode = LDAC_CHANNEL_MODE_STEREO | LDAC_CHANNEL_MODE_DUAL;

    DevCtx->IsLdacNegotiated = TRUE;
    DevCtx->SampleRate = 96000;
    DevCtx->BitDepth = 24;
    DevCtx->BitrateKbps = 990;

    LDAC_DEBUG_PRINT("Injected Sony LDAC Codec Capabilities (96kHz / 24-bit / 990kbps HQ)");
    return STATUS_SUCCESS;
}

/*
 * LdacFilterInspectAclPacket: Parses L2CAP ACL transfer for AVDTP signaling
 */
NTSTATUS LdacFilterInspectAclPacket(
    _In_ PFILTER_DEVICE_CONTEXT DevCtx,
    _In_ struct _BRB_L2CA_ACL_TRANSFER *AclTransfer
) {
    if (!AclTransfer || !AclTransfer->Buffer || AclTransfer->BufferSize < sizeof(AVDTP_HEADER)) {
        return STATUS_SUCCESS;
    }

    PUCHAR data = (PUCHAR)AclTransfer->Buffer;
    PAVDTP_HEADER hdr = (PAVDTP_HEADER)data;
    UCHAR msgType = hdr->MsgTypeAndPktType & 0x03;
    UCHAR sigId = hdr->SignalId & 0x3F;

    switch (sigId) {
    case AVDTP_SIG_DISCOVER:
        LDAC_DEBUG_PRINT("AVDTP DISCOVER transaction detected (MsgType: %u)", msgType);
        break;

    case AVDTP_SIG_GET_CAPABILITIES:
    case AVDTP_SIG_GET_ALL_CAPS:
        LDAC_DEBUG_PRINT("AVDTP GET_CAPABILITIES transaction detected (MsgType: %u)", msgType);
        if (msgType == 0x02) { /* Response Accept */
            /* Scan response payload for Sony Vendor Codec ID */
            for (ULONG i = 2; i + sizeof(SONY_LDAC_CAPABILITY_ELEMENT) <= AclTransfer->BufferSize; i++) {
                if (data[i] == AVDTP_SERVICE_MEDIA_CODEC && data[i + 1] >= 10) {
                    if (data[i + 2] == 0x00 && data[i + 3] == 0xFF) { /* Audio + Vendor Specific */
                        ULONG vendor = *(ULONG*)&data[i + 4];
                        USHORT codec = *(USHORT*)&data[i + 8];
                        if (vendor == SONY_VENDOR_ID && codec == SONY_LDAC_CODEC_ID) {
                            DevCtx->IsLdacNegotiated = TRUE;
                            LDAC_DEBUG_PRINT("CONFIRMED: Remote device supports Sony LDAC Vendor Codec (0x012D:0x00AA)!");
                            break;
                        }
                    }
                }
            }
        }
        break;

    case AVDTP_SIG_SET_CONFIGURATION:
        LDAC_DEBUG_PRINT("AVDTP SET_CONFIGURATION transaction detected.");
        DevCtx->IsLdacNegotiated = TRUE;
        break;

    case AVDTP_SIG_START:
        LDAC_DEBUG_PRINT("AVDTP START transaction detected - Audio streaming initialized.");
        DevCtx->IsA2dpStreaming = TRUE;
        KeQuerySystemTime(&DevCtx->StreamStartTime);
        break;

    case AVDTP_SIG_SUSPEND:
    case AVDTP_SIG_CLOSE:
        LDAC_DEBUG_PRINT("AVDTP SUSPEND/CLOSE transaction detected - Audio streaming paused.");
        DevCtx->IsA2dpStreaming = FALSE;
        break;

    default:
        break;
    }

    return STATUS_SUCCESS;
}
