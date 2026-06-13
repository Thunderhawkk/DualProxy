#include "ioctl.h"
#include "vhf.h"
#include "trace.h"

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
    BOOLEAN completeRequest = TRUE;

    device = WdfIoQueueGetDevice(Queue);
    ctx = GetDeviceContext(device);

    switch (IoControlCode)
    {
    case IOCTL_VDS_ACTIVATE:
    {
        status = VhfActivate(ctx);
        break;
    }

    case IOCTL_VDS_DEACTIVATE:
    {
        status = VhfDeactivate(ctx);
        break;
    }

    case IOCTL_VDS_SUBMIT_INPUT:
    {
        // Input report from userspace service (64 bytes USB format)
        if (InputBufferLength < DUALSENSE_INPUT_REPORT_SIZE)
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        // Read the input buffer from the request
        PUCHAR inputReport = NULL;
        size_t bytesRead = 0;

        status = WdfRequestRetrieveInputBuffer(Request, DUALSENSE_INPUT_REPORT_SIZE, &inputReport, &bytesRead);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        status = VhfSubmitInputReport(ctx, inputReport, (ULONG)bytesRead);
        break;
    }

    case IOCTL_VDS_READ_OUTPUT:
    {
        // Read a pending output report from the queue
        if (OutputBufferLength < DUALSENSE_OUTPUT_REPORT_SIZE)
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        // Try to dequeue from the output queue (non-blocking)
        WDFREQUEST outputRequest = NULL;
        status = WdfIoQueueRetrieveNextRequest(ctx->OutputReportQueue, &outputRequest);
        if (status == STATUS_NO_MORE_ENTRIES)
        {
            // No pending output report
            status = STATUS_DEVICE_NOT_READY;
            break;
        }

        if (!NT_SUCCESS(status))
        {
            break;
        }

        // Copy output report from the queued request to the caller
        PUCHAR outputReport = NULL;
        size_t outputBytes = 0;

        status = WdfRequestRetrieveOutputBuffer(Request, DUALSENSE_OUTPUT_REPORT_SIZE, &outputReport, &outputBytes);
        if (!NT_SUCCESS(status))
        {
            WdfRequestComplete(outputRequest, STATUS_SUCCESS);
            break;
        }

        // Copy the VHF output report data
        PUCHAR vhfReport = NULL;
        size_t vhfReportSize = 0;
        NTSTATUS getStatus = WdfRequestRetrieveInputBuffer(outputRequest, 0, &vhfReport, &vhfReportSize);
        if (NT_SUCCESS(getStatus) && vhfReport != NULL && vhfReportSize > 0)
        {
            ULONG copySize = (ULONG)min(vhfReportSize, OutputBufferLength);
            RtlCopyMemory(outputReport, vhfReport, copySize);
            WdfRequestSetInformation(Request, copySize);
            status = STATUS_SUCCESS;
        }
        else
        {
            status = STATUS_UNSUCCESSFUL;
        }

        WdfRequestComplete(outputRequest, STATUS_SUCCESS);
        break;
    }

    case IOCTL_VDS_GET_OUTPUT_COUNT:
    {
        // Return the number of pending output reports
        if (OutputBufferLength < sizeof(ULONG))
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        ULONG count = 0;
        WDFREQUEST peekRequest = NULL;

        // Peek to count pendings (this is approximate - better to track with a counter)
        status = WdfIoQueueRetrieveNextRequest(ctx->OutputReportQueue, &peekRequest);
        while (NT_SUCCESS(status) && peekRequest != NULL)
        {
            count++;
            WdfRequestComplete(peekRequest, STATUS_SUCCESS);
            status = WdfIoQueueRetrieveNextRequest(ctx->OutputReportQueue, &peekRequest);
        }

        // Write count to output
        PUCHAR outputBuf = NULL;
        size_t outputSize = 0;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), &outputBuf, &outputSize);
        if (NT_SUCCESS(status) && outputSize >= sizeof(ULONG))
        {
            *(PULONG)outputBuf = count;
            WdfRequestSetInformation(Request, sizeof(ULONG));
            status = STATUS_SUCCESS;
        }
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestComplete(Request, status);
}
