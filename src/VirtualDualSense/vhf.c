#include "vhf.h"

static
VOID
WriteDiagnosticStatus(
    _In_ PCWSTR ValueName,
    _In_ NTSTATUS Status
)
{
    HANDLE keyHandle;
    UNICODE_STRING keyName;
    OBJECT_ATTRIBUTES objAttr;

    RtlInitUnicodeString(&keyName, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\VirtualDualSense");
    InitializeObjectAttributes(&objAttr, &keyName, OBJ_KERNEL_HANDLE, NULL, NULL);

    NTSTATUS regStatus = ZwOpenKey(&keyHandle, KEY_SET_VALUE, &objAttr);
    if (NT_SUCCESS(regStatus))
    {
        UNICODE_STRING valueName;
        RtlInitUnicodeString(&valueName, ValueName);
        ZwSetValueKey(keyHandle, &valueName, 0, REG_DWORD, &Status, sizeof(Status));
        ZwClose(keyHandle);
    }
}

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

    PDEVICE_OBJECT wdmDevice = WdfDeviceWdmGetDeviceObject(DeviceContext->WdfDevice);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "VirtualDualSense: VhfActivate - FDO=0x%p\n", wdmDevice);

    if (wdmDevice == NULL)
    {
        WriteDiagnosticStatus(L"VhfCreateStatus", 0xC0000001);
        return STATUS_INVALID_PARAMETER;
    }

    VHF_CONFIG_INIT(
        &vhfConfig,
        wdmDevice,
        DUALSENSE_HID_DESCRIPTOR_SIZE,
        (PUCHAR)DualSenseHidDescriptor
    );

    // vhf.sys (build 26100) might expect a smaller structure size
    // than what WDK 10.0.28000.0's sizeof(VHF_CONFIG) produces.
    // Override Size to match what the running vhf.sys expects.
    vhfConfig.Size = sizeof(VHF_CONFIG);

    vhfConfig.VendorID = 0x054C;
    vhfConfig.ProductID = 0x0CE6;
    vhfConfig.VersionNumber = 0x8100;

    // Set hardware IDs in HID format so Windows class driver recognizes
    // this as a DualSense Wireless Controller (matching input.inf mapping
    // for VID 0x054C/PID 0x0CE6/REV 0x8100).
    {
        static const WCHAR hidHardwareIds[] =
            L"HID\\VID_054C&PID_0CE6&REV_8100\0"
            L"HID\\VID_054C&PID_0CE6\0";
        vhfConfig.HardwareIDs = (PWSTR)hidHardwareIds;
        vhfConfig.HardwareIDsLength = sizeof(hidHardwareIds);
    }

    {
        HANDLE keyHandle;
        UNICODE_STRING keyName;
        OBJECT_ATTRIBUTES objAttr;
        RtlInitUnicodeString(&keyName, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\VirtualDualSense");
        InitializeObjectAttributes(&objAttr, &keyName, OBJ_KERNEL_HANDLE, NULL, NULL);
        if (NT_SUCCESS(ZwOpenKey(&keyHandle, KEY_SET_VALUE, &objAttr))) {
            UNICODE_STRING vn;
            RtlInitUnicodeString(&vn, L"DescSize");
            ULONG ds = DUALSENSE_HID_DESCRIPTOR_SIZE;
            ZwSetValueKey(keyHandle, &vn, 0, REG_DWORD, &ds, sizeof(ds));
            RtlInitUnicodeString(&vn, L"ConfigSize");
            ULONG cs = vhfConfig.Size;
            ZwSetValueKey(keyHandle, &vn, 0, REG_DWORD, &cs, sizeof(cs));
            ZwClose(keyHandle);
        }
    }

    vhfConfig.EvtVhfAsyncOperationWriteReport = VirtualDualSenseEvtVhfAsyncWriteReport;
    vhfConfig.EvtVhfAsyncOperationGetFeature = VirtualDualSenseEvtVhfAsyncGetFeature;
    vhfConfig.EvtVhfAsyncOperationSetFeature = VirtualDualSenseEvtVhfAsyncSetFeature;

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "VirtualDualSense: VhfActivate - Size=%lu ClientCtx=0x%p DevObj=0x%p "
        "VID=0x%04X PID=0x%04X Ver=0x%04X DescLen=%hu Desc=0x%p\n",
        vhfConfig.Size, vhfConfig.VhfClientContext, vhfConfig.DeviceObject,
        vhfConfig.VendorID, vhfConfig.ProductID, vhfConfig.VersionNumber,
        vhfConfig.ReportDescriptorLength, vhfConfig.ReportDescriptor);

    status = VhfCreate(&vhfConfig, &DeviceContext->VhfHandle);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "VirtualDualSense: VhfCreate returned 0x%08X\n", status);
    WriteDiagnosticStatus(L"VhfCreateStatus", status);
    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "VirtualDualSense: VhfCreate failed with status 0x%08X\n", status);
        return status;
    }

    status = VhfStart(DeviceContext->VhfHandle);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "VirtualDualSense: VhfStart returned 0x%08X\n", status);
    WriteDiagnosticStatus(L"VhfStartStatus", status);
    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "VirtualDualSense: VhfStart failed with status 0x%08X\n", status);
        VhfDelete(DeviceContext->VhfHandle, TRUE);
        DeviceContext->VhfHandle = NULL;
        return status;
    }

    DeviceContext->VhfActive = TRUE;

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "VirtualDualSense: VhfActivate succeeded\n");

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

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "VirtualDualSense: WriteReport reportId=0x%02X size=%lu\n",
        TransferPacket->reportId, copySize);

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