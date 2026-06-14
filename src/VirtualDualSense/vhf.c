#include "vhf.h"

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
        (PUCHAR)DualSenseHidDescriptor
    );

    vhfConfig.VendorID = DUALSENSE_VID;
    vhfConfig.ProductID = DUALSENSE_PID;
    vhfConfig.VersionNumber = DUALSENSE_VERSION;
    vhfConfig.VhfClientContext = DeviceContext;
    vhfConfig.EvtVhfAsyncOperationWriteReport = VirtualDualSenseEvtVhfAsyncWriteReport;
    vhfConfig.EvtVhfAsyncOperationGetFeature = VirtualDualSenseEvtVhfAsyncGetFeature;
    vhfConfig.EvtVhfAsyncOperationSetFeature = VirtualDualSenseEvtVhfAsyncSetFeature;

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

    // Drain any pending output reports
    for (;;)
    {
        PSLIST_ENTRY entry = InterlockedPopEntrySList(&DeviceContext->OutputReportList);
        if (entry == NULL)
        {
            break;
        }
        ExFreePool(CONTAINING_RECORD(entry, OUTPUT_REPORT_ENTRY, ListEntry));
        InterlockedDecrement(&DeviceContext->OutputReportCount);
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

    status = VhfReadReportSubmit(DeviceContext->VhfHandle, &transferPacket);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    return STATUS_SUCCESS;
}

static
VOID
FillCalibrationReport(
    _Out_writes_bytes_(40) PUCHAR Buffer
)
{
    RtlFillMemory(Buffer, 40, 0);
    Buffer[0] = 0x05;
}

static
VOID
FillPairInfo(
    _Out_writes_bytes_(19) PUCHAR Buffer
)
{
    RtlFillMemory(Buffer, 19, 0);
    Buffer[0] = 0x09;
    Buffer[1] = 0x01;
}

static
VOID
FillRevisionInfo(
    _Out_writes_bytes_(63) PUCHAR Buffer
)
{
    RtlFillMemory(Buffer, 63, 0);
    Buffer[0] = 0x20;
    PUCHAR ts = &Buffer[1];
    ts[0] = '2'; ts[1] = '0'; ts[2] = '2'; ts[3] = '4';
    ts[4] = ':'; ts[5] = '0'; ts[6] = '1';
    ts[7] = ':'; ts[8] = '0'; ts[9] = '1';
    ts[10] = ' '; ts[11] = '0'; ts[12] = '0';
    ts[13] = ':'; ts[14] = '0'; ts[15] = '0';
    ts[16] = ':'; ts[17] = '0'; ts[18] = '0';
    Buffer[27] = 0x04;
    Buffer[28] = 0x00;
}

_Function_class_(EVT_VHF_ASYNC_OPERATION)
VOID
VirtualDualSenseEvtVhfAsyncWriteReport(
    _In_ PVOID VhfClientContext,
    _In_ VHFOPERATIONHANDLE VhfOperationHandle,
    _In_opt_ PVOID VhfOperationContext,
    _In_ PHID_XFER_PACKET TransferPacket
)
{
    UNREFERENCED_PARAMETER(VhfOperationContext);
    PDEVICE_CONTEXT ctx = (PDEVICE_CONTEXT)VhfClientContext;

    if (ctx == NULL || !ctx->VhfActive || TransferPacket == NULL)
    {
        VhfAsyncOperationComplete(VhfOperationHandle, STATUS_SUCCESS);
        return;
    }

    if (TransferPacket->reportBuffer == NULL || TransferPacket->reportBufferLen == 0)
    {
        VhfAsyncOperationComplete(VhfOperationHandle, STATUS_SUCCESS);
        return;
    }

    ULONG copySize = TransferPacket->reportBufferLen;
    if (copySize > DUALSENSE_OUTPUT_REPORT_SIZE)
    {
        copySize = DUALSENSE_OUTPUT_REPORT_SIZE;
    }

    POUTPUT_REPORT_ENTRY entry = (POUTPUT_REPORT_ENTRY)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(OUTPUT_REPORT_ENTRY),
        'DSVO'
    );

    if (entry == NULL)
    {
        VhfAsyncOperationComplete(VhfOperationHandle, STATUS_SUCCESS);
        return;
    }

    RtlCopyMemory(entry->Data, TransferPacket->reportBuffer, copySize);
    entry->Size = copySize;

    InterlockedPushEntrySList(&ctx->OutputReportList, &entry->ListEntry);
    InterlockedIncrement(&ctx->OutputReportCount);

    VhfAsyncOperationComplete(VhfOperationHandle, STATUS_SUCCESS);
}

_Function_class_(EVT_VHF_ASYNC_OPERATION)
VOID
VirtualDualSenseEvtVhfAsyncGetFeature(
    _In_ PVOID VhfClientContext,
    _In_ VHFOPERATIONHANDLE VhfOperationHandle,
    _In_opt_ PVOID VhfOperationContext,
    _In_ PHID_XFER_PACKET TransferPacket
)
{
    UNREFERENCED_PARAMETER(VhfOperationContext);
    PDEVICE_CONTEXT ctx = (PDEVICE_CONTEXT)VhfClientContext;

    if (ctx == NULL || !ctx->VhfActive || TransferPacket == NULL)
    {
        VhfAsyncOperationComplete(VhfOperationHandle, STATUS_SUCCESS);
        return;
    }

    UCHAR reportId = TransferPacket->reportId;
    PUCHAR buffer = TransferPacket->reportBuffer;
    ULONG bufLen = TransferPacket->reportBufferLen;

    switch (reportId)
    {
    case 0x05:
        if (bufLen >= 40)
        {
            FillCalibrationReport(buffer);
            TransferPacket->reportBufferLen = 40;
        }
        break;

    case 0x09:
        if (bufLen >= 19)
        {
            FillPairInfo(buffer);
            TransferPacket->reportBufferLen = 19;
        }
        break;

    case 0x20:
        if (bufLen >= 63)
        {
            FillRevisionInfo(buffer);
            TransferPacket->reportBufferLen = 63;
        }
        break;

    case 0x22:
        if (bufLen >= 63)
        {
            RtlFillMemory(buffer, 63, 0);
            buffer[0] = 0x22;
            TransferPacket->reportBufferLen = 63;
        }
        break;

    default:
        if (buffer != NULL && bufLen > 0)
        {
            RtlFillMemory(buffer, bufLen, 0);
            buffer[0] = reportId;
        }
        break;
    }

    VhfAsyncOperationComplete(VhfOperationHandle, STATUS_SUCCESS);
}

_Function_class_(EVT_VHF_ASYNC_OPERATION)
VOID
VirtualDualSenseEvtVhfAsyncSetFeature(
    _In_ PVOID VhfClientContext,
    _In_ VHFOPERATIONHANDLE VhfOperationHandle,
    _In_opt_ PVOID VhfOperationContext,
    _In_ PHID_XFER_PACKET TransferPacket
)
{
    UNREFERENCED_PARAMETER(VhfClientContext);
    UNREFERENCED_PARAMETER(VhfOperationContext);
    UNREFERENCED_PARAMETER(TransferPacket);

    VhfAsyncOperationComplete(VhfOperationHandle, STATUS_SUCCESS);
}