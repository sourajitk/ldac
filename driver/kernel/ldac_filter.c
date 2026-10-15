/*
 * Sony LDAC Bluetooth Audio KMDF Upper Filter Driver
 * DriverEntry, Device Initialization, and WDF I/O Queue Handlers
 * Copyright (c) 2026 LDAC-Windows Open Source Project. All rights reserved.
 */

#include "ldac_filter.h"

/* Custom User-Mode Control IOCTLs for Control Panel */
#define IOCTL_LDAC_GET_STATUS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_LDAC_SET_BITRATE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _LDAC_DRIVER_STATUS {
    ULONG   SampleRate;
    ULONG   BitrateKbps;
    BOOLEAN IsLdacActive;
    ULONG   PacketsSent;
    ULONG   PacketsDropped;
} LDAC_DRIVER_STATUS, *PLDAC_DRIVER_STATUS;

/*
 * DriverEntry: Standard KMDF entry point
 */
NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
) {
    WDF_DRIVER_CONFIG config;
    NTSTATUS status;

    LDAC_DEBUG_PRINT("Initializing Sony LDAC KMDF Upper Filter Driver...");

    WDF_DRIVER_CONFIG_INIT(&config, LdacFilterEvtDeviceAdd);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE
    );

    if (!NT_SUCCESS(status)) {
        LDAC_ERROR_PRINT("WdfDriverCreate failed with status 0x%08X", status);
    } else {
        LDAC_DEBUG_PRINT("KMDF Driver created successfully.");
    }

    return status;
}

/*
 * LdacFilterEvtDeviceAdd: Attach filter to the A2DP Bluetooth device stack
 */
NTSTATUS LdacFilterEvtDeviceAdd(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
) {
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFDEVICE device;
    PFILTER_DEVICE_CONTEXT devCtx;
    WDF_IO_QUEUE_CONFIG queueConfig;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);

    LDAC_DEBUG_PRINT("LdacFilterEvtDeviceAdd: Attaching as Upper Filter to A2DP device...");

    /* 1. Mark this device as a filter driver (KMDF requirement) */
    WdfFdoInitSetFilter(DeviceInit);

    /* 2. Allocate and associate device context */
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, FILTER_DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = LdacFilterEvtDeviceContextCleanup;

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        LDAC_ERROR_PRINT("WdfDeviceCreate failed with status 0x%08X", status);
        return status;
    }

    devCtx = GetFilterDeviceContext(device);
    RtlZeroMemory(devCtx, sizeof(FILTER_DEVICE_CONTEXT));
    devCtx->WdfDevice = device;
    devCtx->LowerTarget = WdfDeviceGetDefaultIoTarget(device);
    devCtx->SampleRate = 96000;
    devCtx->BitDepth = 24;
    devCtx->BitrateKbps = 990;
    devCtx->ChannelMode = LDAC_CHANNEL_MODE_STEREO;
    devCtx->IsLdacNegotiated = FALSE;
    devCtx->IsA2dpStreaming = FALSE;

    /* 3. Configure Default Parallel I/O Queue to intercept BRB and control requests */
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoInternalDeviceControl = LdacFilterEvtIoInternalDeviceControl;
    queueConfig.EvtIoDeviceControl = LdacFilterEvtIoDeviceControl;

    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) {
        LDAC_ERROR_PRINT("WdfIoQueueCreate failed with status 0x%08X", status);
        return status;
    }

    LDAC_DEBUG_PRINT("LDAC Upper Filter successfully attached to A2DP device.");
    return STATUS_SUCCESS;
}

/*
 * EvtDeviceContextCleanup: Resource teardown when device is unplugged or disabled
 */
VOID LdacFilterEvtDeviceContextCleanup(
    _In_ WDFOBJECT Object
) {
    PFILTER_DEVICE_CONTEXT devCtx = GetFilterDeviceContext((WDFDEVICE)Object);
    if (devCtx) {
        LDAC_DEBUG_PRINT("Cleaning up LDAC Device Context for device %p", devCtx->WdfDevice);
    }
}

/*
 * LdacFilterEvtIoInternalDeviceControl: Intercepts kernel BRB requests
 */
VOID LdacFilterEvtIoInternalDeviceControl(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     OutputBufferLength,
    _In_ size_t     InputBufferLength,
    _In_ ULONG      IoControlCode
) {
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PFILTER_DEVICE_CONTEXT devCtx = GetFilterDeviceContext(device);

    /* Check for Bluetooth Request Block (BRB) submission */
    if (IoControlCode == IOCTL_INTERNAL_BTH_SUBMIT_BRB) {
        NTSTATUS status = LdacProcessSubmitBrb(devCtx, Request, OutputBufferLength, InputBufferLength);
        if (status == STATUS_SUCCESS) {
            /* Handled or forwarded asynchronously by BRB processor */
            return;
        }
    }

    /* Pass through all other unhandled internal I/O controls directly down the stack */
    WDF_REQUEST_SEND_OPTIONS options;
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

    if (!WdfRequestSend(Request, devCtx->LowerTarget, &options)) {
        NTSTATUS status = WdfRequestGetStatus(Request);
        WdfRequestComplete(Request, status);
    }
}

/*
 * LdacFilterEvtIoDeviceControl: Handles user-mode queries from Control Panel GUI
 */
VOID LdacFilterEvtIoDeviceControl(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     OutputBufferLength,
    _In_ size_t     InputBufferLength,
    _In_ ULONG      IoControlCode
) {
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PFILTER_DEVICE_CONTEXT devCtx = GetFilterDeviceContext(device);
    NTSTATUS status = STATUS_SUCCESS;
    size_t bytesReturned = 0;

    switch (IoControlCode) {
    case IOCTL_LDAC_GET_STATUS: {
        if (OutputBufferLength < sizeof(LDAC_DRIVER_STATUS)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        PLDAC_DRIVER_STATUS pOut = NULL;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(LDAC_DRIVER_STATUS), (PVOID*)&pOut, NULL);
        if (NT_SUCCESS(status) && pOut) {
            pOut->SampleRate = devCtx->SampleRate;
            pOut->BitrateKbps = devCtx->BitrateKbps;
            pOut->IsLdacActive = devCtx->IsLdacNegotiated;
            pOut->PacketsSent = devCtx->PacketsTransmitted;
            pOut->PacketsDropped = devCtx->PacketsDropped;
            bytesReturned = sizeof(LDAC_DRIVER_STATUS);
        }
        break;
    }

    case IOCTL_LDAC_SET_BITRATE: {
        if (InputBufferLength < sizeof(ULONG)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        PULONG pNewRate = NULL;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(ULONG), (PVOID*)&pNewRate, NULL);
        if (NT_SUCCESS(status) && pNewRate) {
            ULONG rate = *pNewRate;
            if (rate == 990 || rate == 660 || rate == 330) {
                devCtx->BitrateKbps = rate;
                LDAC_DEBUG_PRINT("Updated active LDAC bitrate to %lu kbps", rate);
            } else {
                status = STATUS_INVALID_PARAMETER;
            }
        }
        break;
    }

    default:
        /* Forward all other device controls to the lower driver */
        {
            WDF_REQUEST_SEND_OPTIONS options;
            WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
            if (!WdfRequestSend(Request, devCtx->LowerTarget, &options)) {
                status = WdfRequestGetStatus(Request);
                WdfRequestComplete(Request, status);
            }
            return;
        }
    }

    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}
