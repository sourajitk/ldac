/*
 * Sony LDAC Bluetooth Audio KMDF Upper Filter Driver
 * Bluetooth Request Block (BRB) Interceptor and AVDTP Dispatcher
 * Copyright (c) 2026 LDAC-Windows Open Source Project. All rights reserved.
 */

#include "ldac_filter.h"

/* Completion Context for Asynchronous BRB Processing */
typedef struct _BRB_COMPLETION_CONTEXT {
    PFILTER_DEVICE_CONTEXT  DevCtx;
    PBRB_HEADER             Brb;
    WDFREQUEST              Request;
} BRB_COMPLETION_CONTEXT, *PBRB_COMPLETION_CONTEXT;

/*
 * LdacBrbCompletionRoutine: Handles asynchronous completion of submitted BRBs
 */
static VOID LdacBrbCompletionRoutine(
    _In_ WDFREQUEST                     Request,
    _In_ WDFIOTARGET                    Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_ WDFCONTEXT                     Context
) {
    PBRB_COMPLETION_CONTEXT compCtx = (PBRB_COMPLETION_CONTEXT)Context;
    PFILTER_DEVICE_CONTEXT devCtx = compCtx->DevCtx;
    PBRB_HEADER brb = compCtx->Brb;
    NTSTATUS status = Params->IoStatus.Status;

    UNREFERENCED_PARAMETER(Target);

    if (NT_SUCCESS(status) && brb != NULL) {
        switch (brb->Type) {
        case BRB_L2CA_OPEN_CHANNEL: {
            struct _BRB_L2CA_OPEN_CHANNEL *openBrb = (struct _BRB_L2CA_OPEN_CHANNEL*)brb;
            if (openBrb->Psm == AVDTP_PSM) {
                devCtx->L2capChannelHandle = openBrb->ChannelHandle;
                devCtx->RemoteDeviceAddress = openBrb->BtAddress;
                LDAC_DEBUG_PRINT("AVDTP L2CAP Channel Opened successfully to BD_ADDR: %012llX", 
                                 (unsigned long long)devCtx->RemoteDeviceAddress);
            }
            break;
        }

        case BRB_L2CA_ACL_TRANSFER: {
            struct _BRB_L2CA_ACL_TRANSFER *aclBrb = (struct _BRB_L2CA_ACL_TRANSFER*)brb;
            if (NT_SUCCESS(aclBrb->Hdr.Status)) {
                devCtx->PacketsTransmitted++;
            } else {
                devCtx->PacketsDropped++;
            }
            break;
        }

        case BRB_L2CA_CLOSE_CHANNEL: {
            LDAC_DEBUG_PRINT("AVDTP L2CAP Channel Closed.");
            devCtx->L2capChannelHandle = NULL;
            devCtx->IsA2dpStreaming = FALSE;
            devCtx->IsLdacNegotiated = FALSE;
            break;
        }

        default:
            break;
        }
    }

    /* Complete original WDF request with final I/O status */
    WdfRequestComplete(Request, status);
    ExFreePoolWithTag(compCtx, LDAC_POOL_TAG);
}

/*
 * LdacProcessSubmitBrb: Inspects and routes IOCTL_INTERNAL_BTH_SUBMIT_BRB
 */
NTSTATUS LdacProcessSubmitBrb(
    _In_ PFILTER_DEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST             Request,
    _In_ size_t                 OutputBufferLength,
    _In_ size_t                 InputBufferLength
) {
    WDF_REQUEST_PARAMETERS params;
    PBRB_HEADER brb;
    WDF_REQUEST_SEND_OPTIONS sendOptions;
    PBRB_COMPLETION_CONTEXT compCtx;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    WDF_REQUEST_PARAMETERS_INIT(&params);
    WdfRequestGetParameters(Request, &params);

    /* Retrieve BRB pointer from Argument1 */
    brb = (PBRB_HEADER)params.Parameters.Others.Argument1;
    if (!brb) {
        return STATUS_INVALID_PARAMETER;
    }

    switch (brb->Type) {
    case BRB_L2CA_OPEN_CHANNEL: {
        struct _BRB_L2CA_OPEN_CHANNEL *openBrb = (struct _BRB_L2CA_OPEN_CHANNEL*)brb;
        if (openBrb->Psm == AVDTP_PSM) {
            LDAC_DEBUG_PRINT("Intercepted BRB_L2CA_OPEN_CHANNEL for AVDTP PSM 0x0019");
            DevCtx->RemoteDeviceAddress = openBrb->BtAddress;
        }
        break;
    }

    case BRB_L2CA_ACL_TRANSFER: {
        struct _BRB_L2CA_ACL_TRANSFER *aclBrb = (struct _BRB_L2CA_ACL_TRANSFER*)brb;
        /* Inspect and inject Sony LDAC capability frames */
        LdacFilterInspectAclPacket(DevCtx, aclBrb);
        break;
    }

    case BRB_L2CA_CLOSE_CHANNEL: {
        LDAC_DEBUG_PRINT("Intercepted BRB_L2CA_CLOSE_CHANNEL");
        break;
    }

    default:
        break;
    }

    /* Allocate completion context to monitor outcome asynchronously */
    compCtx = (PBRB_COMPLETION_CONTEXT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(BRB_COMPLETION_CONTEXT),
        LDAC_POOL_TAG
    );

    if (compCtx == NULL) {
        /* Fallback: Send and forget if memory allocation fails */
        WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
        if (!WdfRequestSend(Request, DevCtx->LowerTarget, &sendOptions)) {
            NTSTATUS sendStatus = WdfRequestGetStatus(Request);
            WdfRequestComplete(Request, sendStatus);
        }
        return STATUS_SUCCESS;
    }

    compCtx->DevCtx = DevCtx;
    compCtx->Brb = brb;
    compCtx->Request = Request;

    WdfRequestFormatRequestUsingCurrentType(Request);
    WdfRequestSetCompletionRoutine(Request, LdacBrbCompletionRoutine, compCtx);

    WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions, 0);
    if (!WdfRequestSend(Request, DevCtx->LowerTarget, &sendOptions)) {
        NTSTATUS sendStatus = WdfRequestGetStatus(Request);
        ExFreePoolWithTag(compCtx, LDAC_POOL_TAG);
        WdfRequestComplete(Request, sendStatus);
    }

    return STATUS_SUCCESS;
}
