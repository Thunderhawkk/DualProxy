#pragma once

#include <wdm.h>
#include <wdf.h>
#include <vhf.h>
#include "hiddescriptor.h"

#define DEVICE_NAME_PREFIX L"\\Device\\VirtualDualSense"
#define SYMLINK_NAME_PREFIX L"\\DosDevices\\VirtualDualSense"
#define MAX_INSTANCES 4
#define FILE_DEVICE_VIRTUAL_DUALSENSE 0x8601

#define CTL_CODE_VDS(Function) \
    CTL_CODE(FILE_DEVICE_VIRTUAL_DUALSENSE, Function, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_VDS_ACTIVATE           CTL_CODE_VDS(0x800)
#define IOCTL_VDS_DEACTIVATE         CTL_CODE_VDS(0x801)
#define IOCTL_VDS_SUBMIT_INPUT       CTL_CODE_VDS(0x802)
#define IOCTL_VDS_READ_OUTPUT        CTL_CODE_VDS(0x803)
#define IOCTL_VDS_GET_OUTPUT_COUNT   CTL_CODE_VDS(0x804)

typedef struct _DEVICE_CONTEXT {
    WDFDEVICE WdfDevice;
    WDFQUEUE IoQueue;
    VHFHANDLE VhfHandle;
    BOOLEAN VhfActive;
    WDFQUEUE OutputReportQueue;
    WDFWAITLOCK OutputLock;
    LONG InstanceIndex;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)

typedef struct _DRIVER_CONTEXT {
    ULONG NextInstance;
} DRIVER_CONTEXT, *PDRIVER_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DRIVER_CONTEXT, GetDriverContext)

EVT_WDF_DRIVER_DEVICE_ADD              VirtualDualSenseEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE        VirtualDualSenseEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE        VirtualDualSenseEvtDeviceReleaseHardware;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL     VirtualDualSenseEvtIoDeviceControl;
EVT_WDF_DEVICE_SELF_MANAGED_IO_CLEANUP VirtualDualSenseEvtSelfManagedIoCleanup;

EVT_VHF_ASYNC_OPERATION               VirtualDualSenseEvtVhfAsyncOperation;
