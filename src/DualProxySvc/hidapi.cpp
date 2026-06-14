#include "hidapi.h"
#include <memory.h>
#include <stdlib.h>
#include <hidsdi.h>
#include <hidpi.h>

// DEFINE_GUID from devguid.h / hidsdi.h for the HID interface class
// {4d1e55b2-f16f-11cf-88cb-001111000030}
static const GUID GUID_DEVINTERFACE_HID = {0x4d1e55b2, 0xf16f, 0x11cf, {0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};

#define DUALSENSE_VID 0x054C
#define DUALSENSE_PID_USB  0x0CE6
#define DUALSENSE_PID_BT   0x0CE6  // BT uses same PID on Windows
#define DUALSENSE_INPUT_REPORT_SIZE 64

bool HIDApi::FindDualSense(DualSenseDevice& device)
{
    HDEVINFO deviceInfoSet = SetupDiGetClassDevs(
        &GUID_DEVINTERFACE_HID,
        NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );
    if (deviceInfoSet == INVALID_HANDLE_VALUE)
        return false;

    // Prefer 78-byte native BT interface (actual gamepad input source).
    // Fall back to 64-byte USB proxy interface (output-only compatibility shim).
    DualSenseDevice preferred = {};
    preferred.Handle = INVALID_HANDLE_VALUE;
    DualSenseDevice fallback = {};
    fallback.Handle = INVALID_HANDLE_VALUE;

    SP_DEVICE_INTERFACE_DATA interfaceData;
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    for (DWORD i = 0; ; i++)
    {
        if (!SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &GUID_DEVINTERFACE_HID, i, &interfaceData))
            break;

        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &interfaceData, NULL, 0, &requiredSize, NULL);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            continue;

        PSP_DEVICE_INTERFACE_DETAIL_DATA detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(requiredSize);
        if (!detailData) continue;
        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        if (!SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &interfaceData, detailData, requiredSize, NULL, NULL))
        {
            free(detailData);
            continue;
        }

        HANDLE hDevice = CreateFile(
            detailData->DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            NULL
        );

        if (hDevice == INVALID_HANDLE_VALUE)
        {
            free(detailData);
            continue;
        }

        HIDD_ATTRIBUTES attrs;
        attrs.Size = sizeof(HIDD_ATTRIBUTES);
        bool keepHandle = false;

        if (HidD_GetAttributes(hDevice, &attrs) &&
            attrs.VendorID == DUALSENSE_VID && attrs.ProductID == DUALSENSE_PID_BT)
        {
            PHIDP_PREPARSED_DATA ppd = NULL;
            if (HidD_GetPreparsedData(hDevice, &ppd))
            {
                HIDP_CAPS caps;
                if (HidP_GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS &&
                    caps.UsagePage == 0x01 && caps.Usage == 0x05 &&
                    caps.InputReportByteLength >= 64)
                {
                    DWORD reportSize = caps.InputReportByteLength;

                    bool isBt = (wcsstr(detailData->DevicePath, L"BTH") != NULL) ||
                                (wcsstr(detailData->DevicePath, L"Bluetooth") != NULL) ||
                                (wcsstr(detailData->DevicePath, L"00001124") != NULL);

                    if (reportSize > DUALSENSE_INPUT_REPORT_SIZE && preferred.Handle == INVALID_HANDLE_VALUE)
                    {
                        // 78-byte native BT interface — preferred input source
                        preferred.Handle = hDevice;
                        preferred.Vid = attrs.VendorID;
                        preferred.Pid = attrs.ProductID;
                        wcsncpy_s(preferred.DevicePath, detailData->DevicePath, _TRUNCATE);
                        GetSerialString(hDevice, preferred.Serial, 64);
                        preferred.IsBluetooth = isBt;
                        preferred.InputReportByteLength = reportSize;
                        keepHandle = true;
                    }
                    else if (reportSize == DUALSENSE_INPUT_REPORT_SIZE && fallback.Handle == INVALID_HANDLE_VALUE)
                    {
                        // 64-byte USB proxy interface — save as fallback
                        fallback.Handle = hDevice;
                        fallback.Vid = attrs.VendorID;
                        fallback.Pid = attrs.ProductID;
                        wcsncpy_s(fallback.DevicePath, detailData->DevicePath, _TRUNCATE);
                        GetSerialString(hDevice, fallback.Serial, 64);
                        fallback.IsBluetooth = isBt;
                        fallback.InputReportByteLength = reportSize;
                        keepHandle = true;
                    }
                }
                HidD_FreePreparsedData(ppd);
            }
        }

        if (!keepHandle)
            CloseHandle(hDevice);
        free(detailData);
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    if (preferred.Handle != INVALID_HANDLE_VALUE)
    {
        device = preferred;
        if (fallback.Handle != INVALID_HANDLE_VALUE)
            CloseHandle(fallback.Handle);
        return true;
    }

    if (fallback.Handle != INVALID_HANDLE_VALUE)
    {
        device = fallback;
        return true;
    }

    device.Handle = INVALID_HANDLE_VALUE;
    return false;
}

bool HIDApi::ReadInput(HANDLE handle, BYTE* buffer, DWORD size, DWORD timeoutMs)
{
    if (handle == INVALID_HANDLE_VALUE || handle == NULL)
    {
        return false;
    }

    UNREFERENCED_PARAMETER(timeoutMs);

    // Try synchronous ReadFile first, fall back to HidD_GetInputReport
    OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent)
    {
        buffer[0] = 0x01;
        return HidD_GetInputReport(handle, buffer, size);
    }

    DWORD bytesRead = 0;
    bool success = false;

    if (ReadFile(handle, buffer, size, &bytesRead, &ov))
    {
        success = (bytesRead > 0);
    }
    else
    {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING)
        {
            DWORD waitResult = WaitForSingleObject(ov.hEvent, timeoutMs);
            if (waitResult == WAIT_OBJECT_0)
            {
                if (GetOverlappedResult(handle, &ov, &bytesRead, FALSE))
                {
                    success = (bytesRead > 0);
                }
            }
            else
            {
                CancelIo(handle);
                GetOverlappedResult(handle, &ov, &bytesRead, FALSE);
            }
        }
    }

    CloseHandle(ov.hEvent);

    if (!success)
    {
        buffer[0] = 0x01;
        success = HidD_GetInputReport(handle, buffer, size);
    }

    return success;
}

bool HIDApi::ReadInputFromPath(const wchar_t* devicePath, BYTE* buffer, DWORD size, DWORD timeoutMs)
{
    if (!devicePath || !buffer || size == 0)
    {
        return false;
    }

    UNREFERENCED_PARAMETER(timeoutMs);

    // Log the device path (first call only for brevity)
    static bool pathLogged = false;
    if (!pathLogged)
    {
        char pathA[512];
        WideCharToMultiByte(CP_ACP, 0, devicePath, -1, pathA, sizeof(pathA), NULL, NULL);
        pathLogged = true;
    }

    HANDLE hDevice = CreateFile(
        devicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hDevice == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    // Diagnostic: query caps
    static bool capsLogged = false;
    if (!capsLogged)
    {
        PHIDP_PREPARSED_DATA ppd = NULL;
        if (HidD_GetPreparsedData(hDevice, &ppd))
        {
            HIDP_CAPS caps;
            if (HidP_GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS)
            {
                capsLogged = true;
            }
            HidD_FreePreparsedData(ppd);
        }
    }

    // Try synchronous ReadFile, then fallback to HidD_GetInputReport
    DWORD bytesRead = 0;
    bool success = false;
    DWORD lastErr = 0;

    if (ReadFile(hDevice, buffer, size, &bytesRead, NULL))
    {
        success = (bytesRead > 0);
    }
    else
    {
        lastErr = GetLastError();

        // Fallback: try HidD_GetInputReport
        buffer[0] = 0x01;
        success = HidD_GetInputReport(hDevice, buffer, size);
        if (!success)
        {
            lastErr = GetLastError();
        }
    }

    CloseHandle(hDevice);

    if (success)
    {
        SetLastError(ERROR_SUCCESS);
    }
    else
    {
        SetLastError(lastErr);
    }

    return success;
}

bool HIDApi::WriteOutput(HANDLE handle, const BYTE* buffer, DWORD size)
{
    if (handle == INVALID_HANDLE_VALUE || handle == NULL)
    {
        return false;
    }

    // Try synchronous WriteFile first (interrupt out pipe), then fall back to
    // HidD_SetOutputReport (control pipe). BT DualSense often requires the
    // control pipe for output reports.
    DWORD bytesWritten = 0;
    if (WriteFile(handle, buffer, size, &bytesWritten, NULL) && bytesWritten == size)
    {
        return true;
    }

    return HidD_SetOutputReport(handle, (PVOID)buffer, size);
}

bool HIDApi::SetFeature(HANDLE handle, const BYTE* buffer, DWORD size)
{
    if (handle == INVALID_HANDLE_VALUE || handle == NULL)
    {
        return false;
    }

    return HidD_SetFeature(handle, (PVOID)buffer, size);
}

bool HIDApi::GetFeature(HANDLE handle, BYTE* buffer, DWORD size)
{
    if (handle == INVALID_HANDLE_VALUE || handle == NULL)
    {
        return false;
    }

    return HidD_GetFeature(handle, buffer, size);
}

DWORD HIDApi::GetInputReportSize(HANDLE handle)
{
    if (handle == INVALID_HANDLE_VALUE || handle == NULL)
    {
        return 0;
    }

    PHIDP_PREPARSED_DATA preparsedData = NULL;
    if (!HidD_GetPreparsedData(handle, &preparsedData))
    {
        return 0;
    }

    HIDP_CAPS caps;
    NTSTATUS status = HidP_GetCaps(preparsedData, &caps);
    HidD_FreePreparsedData(preparsedData);

    if (status != HIDP_STATUS_SUCCESS)
    {
        return 0;
    }

    return caps.InputReportByteLength;
}

void HIDApi::Close(HANDLE& handle)
{
    if (handle != INVALID_HANDLE_VALUE && handle != NULL)
    {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
}

bool HIDApi::GetDeviceAttributes(HANDLE handle, USHORT& vid, USHORT& pid)
{
    HIDD_ATTRIBUTES attrs;
    attrs.Size = sizeof(HIDD_ATTRIBUTES);

    if (HidD_GetAttributes(handle, &attrs))
    {
        vid = attrs.VendorID;
        pid = attrs.ProductID;
        return true;
    }
    return false;
}

bool HIDApi::GetManufacturerString(HANDLE handle, wchar_t* buffer, ULONG size)
{
    return HidD_GetManufacturerString(handle, buffer, size);
}

bool HIDApi::GetProductString(HANDLE handle, wchar_t* buffer, ULONG size)
{
    return HidD_GetProductString(handle, buffer, size);
}

bool HIDApi::GetSerialString(HANDLE handle, wchar_t* buffer, ULONG size)
{
    return HidD_GetSerialNumberString(handle, buffer, size);
}
