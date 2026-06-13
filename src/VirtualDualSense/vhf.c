#include "vhf.h"
#include "trace.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VhfActivate)
#pragma alloc_text(PAGE, VhfDeactivate)
#endif

NTSTATUS
VhfActivate(
    _In_ PDEVICE_CONTEXT DeviceContext
)
{
    VHF_CONFIG vhfConfig;
    NTSTATUS status;

    PAGED_CODE();

    if (DeviceContext->VhfActive)
    {
        return STATUS_SUCCESS;
    }

    VHF_CONFIG_INIT(
        &vhfConfig,
        WdfDeviceWdmGetDeviceObject(DeviceContext->WdfDevice),
        DUALSENSE_HID_DESCRIPTOR_SIZE,
        (PUCHAR)DualSenseHidDescriptor,
        DUALSENSE_INPUT_REPORT_SIZE,
        DUALSENSE_OUTPUT_REPORT_SIZE,
        DUALSENSE_FEATURE_REPORT_SIZE
    );

    vhfConfig.VendorId = DUALSENSE_VID;
    vhfConfig.ProductId = DUALSENSE_PID;
    vhfConfig.VersionNumber = DUALSENSE_VERSION;
    vhfConfig.EvtVhfAsyncOperation = VirtualDualSenseEvtVhfAsyncOperation;
    vhfConfig.VhfOperationContext = DeviceContext;

    status = VhfCreate(&vhfConfig, &DeviceContext->VhfHandle);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    status = VhfStart(DeviceContext->VhfHandle);
    if (!NT_SUCCESS(status))
    {
        VhfDelete(DeviceContext->VhfHandle, TRUE);
        DeviceContext->VhfHandle = NULL;
        return status;
    }

    DeviceContext->VhfActive = TRUE;

    return STATUS_SUCCESS;
}

NTSTATUS
VhfDeactivate(
    _In_ PDEVICE_CONTEXT DeviceContext
)
{
    PAGED_CODE();

    if (!DeviceContext->VhfActive)
    {
        return STATUS_SUCCESS;
    }

    DeviceContext->VhfActive = FALSE;

    if (DeviceContext->VhfHandle)
    {
        VhfDelete(DeviceContext->VhfHandle, TRUE);
        DeviceContext->VhfHandle = NULL;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
VhfSubmitInputReport(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_reads_bytes_(ReportSize) PUCHAR ReportData,
    _In_ ULONG ReportSize
)
{
    HID_XFER_PACKET transferPacket;
    NTSTATUS status;

    if (!DeviceContext->VhfActive || DeviceContext->VhfHandle == NULL)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    transferPacket.reportBuffer = ReportData;
    transferPacket.reportBufferLen = ReportSize;
    transferPacket.reportId = DUALSENSE_INPUT_REPORT_ID;

    status = VhfSubmitReadReport(DeviceContext->VhfHandle, &transferPacket);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    return STATUS_SUCCESS;
}

VOID
VirtualDualSenseEvtVhfAsyncOperation(
    _In_ VHFHANDLE VhfHandle,
    _In_ PVOID VhfOperationContext,
    _In_ PHID_XFER_PACKET TransferPacket
)
{
    PDEVICE_CONTEXT ctx = (PDEVICE_CONTEXT)VhfOperationContext;
    NTSTATUS status;

    if (ctx == NULL || !ctx->VhfActive)
    {
        VhfAsyncOperationComplete(VhfHandle, STATUS_SUCCESS);
        return;
    }

    // Queue the output report for the userspace service to read
    // Create a WDFREQUEST to hold the output report data
    WDFREQUEST outputRequest = NULL;
    WDF_OBJECT_ATTRIBUTES requestAttrs;
    WDF_OBJECT_ATTRIBUTES_INIT(&requestAttrs);
    requestAttrs.ParentObject = ctx->WdfDevice;

    status = WdfRequestCreate(&requestAttrs, ctx->WdfDevice, &outputRequest);
    if (NT_SUCCESS(status) && outputRequest != NULL)
    {
        // Allocate a buffer for the output report
        PUCHAR reportBuffer = (PUCHAR)ExAllocatePool2(
            POOL_FLAG_NON_PAGED,
            DUALSENSE_OUTPUT_REPORT_SIZE,
            'DSVO'
        );

        if (reportBuffer != NULL)
        {
            RtlCopyMemory(
                reportBuffer,
                TransferPacket->reportBuffer,
                min(TransferPacket->reportBufferLen, DUALSENSE_OUTPUT_REPORT_SIZE)
            );

            // Set the buffer as the request's input buffer
            WDF_MEMORY_DESCRIPTOR memDesc;
            WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memDesc, reportBuffer, DUALSENSE_OUTPUT_REPORT_SIZE);

            // Forward to output queue
            WDFREQUEST forwardedReq;
            status = WdfRequestForwardToIoQueue(outputRequest, ctx->OutputReportQueue);
            if (!NT_SUCCESS(status))
            {
                ExFreePoolWithTag(reportBuffer, 'DSVO');
                WdfRequestComplete(outputRequest, status);
            }
            // Request is now owned by OutputReportQueue
        }
        else
        {
            WdfRequestComplete(outputRequest, STATUS_INSUFFICIENT_RESOURCES);
        }
    }

    // Complete the VHF operation
    VhfAsyncOperationComplete(VhfHandle, STATUS_SUCCESS);
}
