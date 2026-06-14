#pragma once

#include <wdm.h>
#include <wdf.h>
#include <vhf.h>
#include "hiddescriptor.h"

#define DEVICE_NAME_PREFIX L"\\Device\\VirtualDualSense"
#define SYMLINK_NAME_PREFIX L"\\DosDevices\\VirtualDualSense"
#define MAX_INSTANCES 4
#define IOCTL_VDS_ACTIVATE           0x222000
#define IOCTL_VDS_DEACTIVATE         0x222004
#define IOCTL_VDS_SUBMIT_INPUT       0x222008
#define IOCTL_VDS_READ_OUTPUT        0x22200C
#define IOCTL_VDS_GET_OUTPUT_COUNT   0x222010
#define IOCTL_VDS_PING               0x222014
#define IOCTL_VDS_GET_DEBUG          0x222018

typedef struct _OUTPUT_REPORT_ENTRY {
    SLIST_ENTRY ListEntry;
    ULONG Size;
    UCHAR Data[DUALSENSE_OUTPUT_REPORT_SIZE];
} OUTPUT_REPORT_ENTRY, *POUTPUT_REPORT_ENTRY;

typedef struct _DEVICE_CONTEXT {
    WDFDEVICE WdfDevice;
    WDFQUEUE IoQueue;
    VHFHANDLE VhfHandle;
    BOOLEAN VhfActive;
    SLIST_HEADER OutputReportList;
    LONG OutputReportCount;
    LONG InstanceIndex;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)

typedef struct _DRIVER_CONTEXT {
    LONG NextInstance;
} DRIVER_CONTEXT, *PDRIVER_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DRIVER_CONTEXT, GetDriverContext)

EVT_WDF_DRIVER_DEVICE_ADD              VirtualDualSenseEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE        VirtualDualSenseEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE        VirtualDualSenseEvtDeviceReleaseHardware;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL     VirtualDualSenseEvtIoDeviceControl;
EVT_WDF_DEVICE_SELF_MANAGED_IO_CLEANUP VirtualDualSenseEvtSelfManagedIoCleanup;

_Function_class_(EVT_VHF_ASYNC_OPERATION)
VOID
VirtualDualSenseEvtVhfAsyncWriteReport(
    _In_ PVOID VhfClientContext,
    _In_ VHFOPERATIONHANDLE VhfOperationHandle,
    _In_opt_ PVOID VhfOperationContext,
    _In_ PHID_XFER_PACKET TransferPacket
);

_Function_class_(EVT_VHF_ASYNC_OPERATION)
VOID
VirtualDualSenseEvtVhfAsyncGetFeature(
    _In_ PVOID VhfClientContext,
    _In_ VHFOPERATIONHANDLE VhfOperationHandle,
    _In_opt_ PVOID VhfOperationContext,
    _In_ PHID_XFER_PACKET TransferPacket
);

_Function_class_(EVT_VHF_ASYNC_OPERATION)
VOID
VirtualDualSenseEvtVhfAsyncSetFeature(
    _In_ PVOID VhfClientContext,
    _In_ VHFOPERATIONHANDLE VhfOperationHandle,
    _In_opt_ PVOID VhfOperationContext,
    _In_ PHID_XFER_PACKET TransferPacket
);
