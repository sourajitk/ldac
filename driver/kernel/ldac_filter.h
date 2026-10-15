/*
 * Sony LDAC Bluetooth Audio KMDF Upper Filter Driver
 * Header Definitions, Context Structures, and Bluetooth DDI Interfaces
 * Copyright (c) 2026 LDAC-Windows Open Source Project. All rights reserved.
 */

#ifndef _LDAC_FILTER_H_
#define _LDAC_FILTER_H_

#include <ntddk.h>
#include <wdf.h>

#define NTSTRSAFE_LIB
#include <ntstrsafe.h>

/* Bluetooth Kernel Headers */
#include <bthdef.h>
#include <bthioctl.h>
#include <bthddi.h>

#define LDAC_POOL_TAG 'CADL' /* "LDAC" little-endian */

#define LDAC_DEBUG_PRINT(Format, ...) \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[LDAC_FILTER] " Format "\n", __VA_ARGS__)

#define LDAC_ERROR_PRINT(Format, ...) \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[LDAC_FILTER_ERROR] " Format "\n", __VA_ARGS__)

/* Sony LDAC Vendor Codec Constants */
#define SONY_VENDOR_ID          0x0000012D
#define SONY_LDAC_CODEC_ID      0x00AA

#define AVDTP_PSM               0x0019

/* LDAC Sampling Rate Capability Bitmasks */
#define LDAC_SAMPLING_FREQ_96000 0x04
#define LDAC_SAMPLING_FREQ_88200 0x08
#define LDAC_SAMPLING_FREQ_48000 0x10
#define LDAC_SAMPLING_FREQ_44100 0x20

/* LDAC Channel Mode Capability Bitmasks */
#define LDAC_CHANNEL_MODE_STEREO 0x01
#define LDAC_CHANNEL_MODE_DUAL   0x02
#define LDAC_CHANNEL_MODE_MONO   0x04

/* Device Context for KMDF Upper Filter */
typedef struct _FILTER_DEVICE_CONTEXT {
    WDFDEVICE               WdfDevice;
    WDFIOTARGET             LowerTarget;
    
    /* Bluetooth Connection State */
    BTH_ADDR                RemoteDeviceAddress;
    L2CAP_CHANNEL_HANDLE    L2capChannelHandle;
    BOOLEAN                 IsLdacNegotiated;
    BOOLEAN                 IsA2dpStreaming;
    
    /* Negotiated Audio Parameters */
    ULONG                   SampleRate;
    ULONG                   BitDepth;
    ULONG                   BitrateKbps;
    UCHAR                   ChannelMode;

    /* Metrics & Diagnostics */
    ULONG                   PacketsTransmitted;
    ULONG                   PacketsDropped;
    LARGE_INTEGER           StreamStartTime;

} FILTER_DEVICE_CONTEXT, *PFILTER_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FILTER_DEVICE_CONTEXT, GetFilterDeviceContext)

/* Driver Callback Function Declarations */
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD LdacFilterEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP LdacFilterEvtDeviceContextCleanup;

/* I/O Queue Callbacks */
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL LdacFilterEvtIoInternalDeviceControl;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL LdacFilterEvtIoDeviceControl;

/* BRB & AVDTP Processing Functions */
NTSTATUS LdacProcessSubmitBrb(
    _In_ PFILTER_DEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength
);

NTSTATUS LdacFilterInspectAclPacket(
    _In_ PFILTER_DEVICE_CONTEXT DevCtx,
    _In_ struct _BRB_L2CA_ACL_TRANSFER *AclTransfer
);

NTSTATUS LdacInjectSonyCapabilities(
    _In_ PFILTER_DEVICE_CONTEXT DevCtx,
    _Inout_ PUCHAR Buffer,
    _In_ ULONG BufferLength
);

#endif /* _LDAC_FILTER_H_ */
