#include "driver.h"
#include <ntstrsafe.h>

DRIVER_INITIALIZE DriverEntry;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, VirtualDualSenseEvtDeviceAdd)
#pragma alloc_text(PAGE, VirtualDualSenseEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, VirtualDualSenseEvtDeviceReleaseHardware)
#endif

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attrs;
    NTSTATUS status;

    WDF_DRIVER_CONFIG_INIT(&config, VirtualDualSenseEvtDeviceAdd);
    config.DriverPoolTag = 'DSVP';

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, DRIVER_CONTEXT);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &attrs,
        &config,
        WDF_NO_HANDLE
    );

    if (!NT_SUCCESS(status))
    {
        return status;
    }

    return status;
}

NTSTATUS
VirtualDualSenseEvtDeviceAdd(
    _In_ WDFDRIVER        Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    WDFDEVICE device;
    WDF_OBJECT_ATTRIBUTES attrs;
    NTSTATUS status;
    UNICODE_STRING symlink;
    PDEVICE_CONTEXT ctx;
    PDRIVER_CONTEXT drvCtx;

    PAGED_CODE();

    drvCtx = GetDriverContext(Driver);

    // Use FileObject context for per-instance identification
    {
        WDF_FILEOBJECT_CONFIG fileConfig;
        WDF_FILEOBJECT_CONFIG_INIT(&fileConfig, NULL, NULL, NULL);
        WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, WDF_NO_OBJECT_ATTRIBUTES);
    }

    // Set PnP/Power callbacks
    {
        WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
        WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
        pnpCallbacks.EvtDevicePrepareHardware = VirtualDualSenseEvtDevicePrepareHardware;
        pnpCallbacks.EvtDeviceReleaseHardware = VirtualDualSenseEvtDeviceReleaseHardware;
        pnpCallbacks.EvtDeviceSelfManagedIoCleanup = VirtualDualSenseEvtSelfManagedIoCleanup;
        WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);
    }

    // Set device characteristics - FDO for root-enumerated
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetExclusive(DeviceInit, FALSE);

    // Create the device
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, DEVICE_CONTEXT);
    status = WdfDeviceCreate(&DeviceInit, &attrs, &device);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    ctx = GetDeviceContext(device);
    ctx->WdfDevice = device;
    ctx->VhfHandle = NULL;
    ctx->VhfActive = FALSE;
    InitializeSListHead(&ctx->OutputReportList);
    ctx->OutputReportCount = 0;

    // Assign instance index
    ctx->InstanceIndex = InterlockedIncrement(&drvCtx->NextInstance) - 1;
    if (ctx->InstanceIndex >= MAX_INSTANCES)
    {
        InterlockedDecrement(&drvCtx->NextInstance);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Create symbolic link
    {
        WCHAR symlinkBuf[64];
        NTSTATUS hr = RtlStringCbPrintfW(symlinkBuf, sizeof(symlinkBuf),
            L"%s%hd", SYMLINK_NAME_PREFIX, ctx->InstanceIndex);
        if (!NT_SUCCESS(hr))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RtlInitUnicodeString(&symlink, symlinkBuf);
        status = WdfDeviceCreateSymbolicLink(device, &symlink);
        if (!NT_SUCCESS(status))
        {
            return status;
        }
    }

    // Create I/O queue for IOCTL dispatch
    {
        WDF_IO_QUEUE_CONFIG queueConfig;
        WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchSequential);
        queueConfig.EvtIoDeviceControl = VirtualDualSenseEvtIoDeviceControl;

        status = WdfIoQueueCreate(
            device,
            &queueConfig,
            WDF_NO_OBJECT_ATTRIBUTES,
            &ctx->IoQueue
        );
        if (!NT_SUCCESS(status))
        {
            return status;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
VirtualDualSenseEvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourceList,
    _In_ WDFCMRESLIST ResourceListTranslated
)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(ResourceList);
    UNREFERENCED_PARAMETER(ResourceListTranslated);

    PAGED_CODE();
    return STATUS_SUCCESS;
}

NTSTATUS
VirtualDualSenseEvtDeviceReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourceListTranslated
)
{
    PDEVICE_CONTEXT ctx;

    UNREFERENCED_PARAMETER(ResourceListTranslated);

    PAGED_CODE();

    ctx = GetDeviceContext(Device);

    if (ctx->VhfActive)
    {
        ctx->VhfActive = FALSE;
        if (ctx->VhfHandle)
        {
            VhfDelete(ctx->VhfHandle, TRUE);
            ctx->VhfHandle = NULL;
        }
    }

    return STATUS_SUCCESS;
}

VOID
VirtualDualSenseEvtSelfManagedIoCleanup(
    _In_ WDFDEVICE Device
)
{
    PDEVICE_CONTEXT ctx = GetDeviceContext(Device);

    if (ctx->VhfActive)
    {
        ctx->VhfActive = FALSE;
        if (ctx->VhfHandle)
        {
            VhfDelete(ctx->VhfHandle, TRUE);
            ctx->VhfHandle = NULL;
        }
    }
}
