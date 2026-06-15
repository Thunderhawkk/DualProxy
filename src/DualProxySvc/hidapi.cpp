#include "hidapi.h"
#include <memory.h>
#include <stdlib.h>
#include <hidsdi.h>
#include <hidpi.h>

static const GUID GUID_DEVINTERFACE_HID = {0x4d1e55b2, 0xf16f, 0x11cf, {0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};

#define DUALSENSE_VID 0x054C
#define DUALSENSE_PID_USB  0x0CE6
#define DUALSENSE_PID_BT   0x0CE6
#define DUALSENSE_INPUT_REPORT_SIZE 64

bool HIDApi::FindDualSense(DualSenseDevice& inputDevice, DualSenseDevice& outputDevice)
{
    HDEVINFO deviceInfoSet = SetupDiGetClassDevs(
        &GUID_DEVINTERFACE_HID,
        NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );
    if (deviceInfoSet == INVALID_HANDLE_VALUE)
        return false;

    inputDevice.Handle = INVALID_HANDLE_VALUE;
    outputDevice.Handle = INVALID_HANDLE_VALUE;

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

        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        if (!SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &interfaceData, detailData, requiredSize, NULL, &devInfoData))
        {
            free(detailData);
            continue;
        }

        WCHAR devInstanceId[256] = {0};
        SetupDiGetDeviceInstanceId(deviceInfoSet, &devInfoData, devInstanceId, 256, NULL);

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
            // Skip VHF virtual devices that share VID/PID with the real DualSense.
            // VHF devices return "Virtual HID Framework (VHF) HID device" from
            // HidD_GetProductString. Real BT DualSense uses the PNP UUID
            // {00001124-...} in its device path (no BTHENUM marker).
            {
                WCHAR productStr[128] = { 0 };
                if (HidD_GetProductString(hDevice, productStr, sizeof(productStr)) &&
                    wcsstr(productStr, L"VHF") != NULL)
                {
                    CloseHandle(hDevice);
                    free(detailData);
                    continue;
                }
            }
            PHIDP_PREPARSED_DATA ppd = NULL;
            if (HidD_GetPreparsedData(hDevice, &ppd))
            {
                HIDP_CAPS caps;
                if (HidP_GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS &&
                    caps.UsagePage == 0x01 && caps.Usage == 0x05 &&
                    caps.InputReportByteLength >= 64)
                {
                    DWORD reportSize = caps.InputReportByteLength;

                    if (reportSize > DUALSENSE_INPUT_REPORT_SIZE &&
                        inputDevice.Handle == INVALID_HANDLE_VALUE)
                    {
                        inputDevice.Handle = hDevice;
                        inputDevice.Vid = attrs.VendorID;
                        inputDevice.Pid = attrs.ProductID;
                        wcsncpy_s(inputDevice.DevicePath, detailData->DevicePath, _TRUNCATE);
                        wcsncpy_s(inputDevice.DeviceInstanceId, devInstanceId, _TRUNCATE);
                        GetSerialString(hDevice, inputDevice.Serial, 64);
                        inputDevice.IsBluetooth = true;
                        inputDevice.InputReportByteLength = reportSize;
                        keepHandle = true;
                    }
                    else if (reportSize == DUALSENSE_INPUT_REPORT_SIZE &&
                             outputDevice.Handle == INVALID_HANDLE_VALUE)
                    {
                        outputDevice.Handle = hDevice;
                        outputDevice.Vid = attrs.VendorID;
                        outputDevice.Pid = attrs.ProductID;
                        wcsncpy_s(outputDevice.DevicePath, detailData->DevicePath, _TRUNCATE);
                        wcsncpy_s(outputDevice.DeviceInstanceId, devInstanceId, _TRUNCATE);
                        GetSerialString(hDevice, outputDevice.Serial, 64);
                        outputDevice.IsBluetooth = false;
                        outputDevice.InputReportByteLength = reportSize;
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

    if (inputDevice.Handle == INVALID_HANDLE_VALUE)
    {
        if (outputDevice.Handle != INVALID_HANDLE_VALUE)
        {
            inputDevice = outputDevice;
            outputDevice.Handle = INVALID_HANDLE_VALUE;
            return true;
        }
        return false;
    }

    return true;
}

bool HIDApi::ReadInput(HANDLE handle, BYTE* buffer, DWORD size, DWORD timeoutMs)
{
    if (handle == INVALID_HANDLE_VALUE || handle == NULL)
    {
        return false;
    }

    UNREFERENCED_PARAMETER(timeoutMs);

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

    HANDLE hDevice = CreateFile(
        devicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (hDevice == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    DWORD bytesRead = 0;
    bool success = false;
    DWORD lastErr = 0;

    OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent)
    {
        // Fallback: synchronous read (may block)
        if (!ReadFile(hDevice, buffer, size, &bytesRead, NULL))
        {
            lastErr = GetLastError();
        }
        else
        {
            success = (bytesRead > 0);
        }
    }
    else
    {
        if (ReadFile(hDevice, buffer, size, NULL, &ov))
        {
            success = true;
            bytesRead = size;
        }
        else
        {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING)
            {
                DWORD waitResult = WaitForSingleObject(ov.hEvent, timeoutMs);
                if (waitResult == WAIT_OBJECT_0)
                {
                    success = GetOverlappedResult(hDevice, &ov, &bytesRead, FALSE);
                    if (!success) lastErr = GetLastError();
                }
                else
                {
                    CancelIo(hDevice);
                    GetOverlappedResult(hDevice, &ov, &bytesRead, FALSE);
                    lastErr = (waitResult == WAIT_TIMEOUT) ? ERROR_TIMEOUT : WAIT_ABANDONED;
                }
            }
            else
            {
                lastErr = err;
            }
        }
        CloseHandle(ov.hEvent);
    }

    if (!success)
    {
        // Fallback: HidD_GetInputReport
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
