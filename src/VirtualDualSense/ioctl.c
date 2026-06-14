#include "ioctl.h"
#include "vhf.h"

VOID
VirtualDualSenseEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
)
{
    WDFDEVICE device;
    PDEVICE_CONTEXT ctx;
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

    device = WdfIoQueueGetDevice(Queue);
    ctx = GetDeviceContext(device);

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "VirtualDualSense: EvtIoDeviceControl called, IoControlCode=0x%08X\n", IoControlCode);

    if (IoControlCode == IOCTL_VDS_PING)
    {
        status = STATUS_SUCCESS;
    }
    else if (IoControlCode == IOCTL_VDS_ACTIVATE)
    {
        status = VhfActivate(ctx);
    }
    else if (IoControlCode == IOCTL_VDS_DEACTIVATE)
    {
        status = VhfDeactivate(ctx);
    }
    else if (IoControlCode == IOCTL_VDS_SUBMIT_INPUT)
    {
        if (InputBufferLength < DUALSENSE_INPUT_REPORT_SIZE)
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            PUCHAR inputReport = NULL;
            size_t bytesRead = 0;

            status = WdfRequestRetrieveInputBuffer(Request, DUALSENSE_INPUT_REPORT_SIZE, &inputReport, &bytesRead);
            if (NT_SUCCESS(status))
            {
                status = VhfSubmitInputReport(ctx, inputReport, (ULONG)bytesRead);
            }
        }
    }
    else if (IoControlCode == IOCTL_VDS_READ_OUTPUT)
    {
        if (OutputBufferLength < DUALSENSE_OUTPUT_REPORT_SIZE)
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            PSLIST_ENTRY listEntry = InterlockedPopEntrySList(&ctx->OutputReportList);
            if (listEntry == NULL)
            {
                status = STATUS_DEVICE_NOT_READY;
            }
            else
            {
                POUTPUT_REPORT_ENTRY entry = CONTAINING_RECORD(listEntry, OUTPUT_REPORT_ENTRY, ListEntry);

                PUCHAR outputReport = NULL;
                size_t outputBytes = 0;

                status = WdfRequestRetrieveOutputBuffer(Request, DUALSENSE_OUTPUT_REPORT_SIZE, &outputReport, &outputBytes);
                if (NT_SUCCESS(status))
                {
                    InterlockedDecrement(&ctx->OutputReportCount);

                    ULONG copySize = entry->Size;
                    if (copySize > (ULONG)outputBytes)
                    {
                        copySize = (ULONG)outputBytes;
                    }
                    RtlCopyMemory(outputReport, entry->Data, copySize);
                    WdfRequestSetInformation(Request, copySize);

                    ExFreePool(entry);
                    status = STATUS_SUCCESS;
                }
                else
                {
                    ExFreePool(entry);
                }
            }
        }
    }
    else if (IoControlCode == IOCTL_VDS_GET_OUTPUT_COUNT)
    {
        if (OutputBufferLength < sizeof(ULONG))
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            ULONG count = (ULONG)InterlockedCompareExchange(&ctx->OutputReportCount, 0, 0);

            PUCHAR outputBuf = NULL;
            size_t outputSize = 0;
            status = WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), &outputBuf, &outputSize);
            if (NT_SUCCESS(status) && outputSize >= sizeof(ULONG))
            {
                *(PULONG)outputBuf = count;
                WdfRequestSetInformation(Request, sizeof(ULONG));
                status = STATUS_SUCCESS;
            }
        }
    }
    else
    {
        status = STATUS_INVALID_DEVICE_REQUEST;
    }

    WdfRequestComplete(Request, status);
}