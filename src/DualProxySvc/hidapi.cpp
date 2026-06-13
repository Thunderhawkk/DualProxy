#include "hidapi.h"
#include <memory.h>
#include <devguid.h>

#define DUALSHENSE_VID 0x054C
#define DUALSHENSE_PID_USB  0x0CE6
#define DUALSHENSE_PID_BT   0x0CE6  // BT uses same PID on Windows

bool HIDApi::FindDualSense(DualSenseDevice& device)
{
    bool found = false;
    HDEVINFO deviceInfoSet = INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DATA interfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA detailData = NULL;
    DWORD detailSize = 0;

    // Get HID device interface set
    deviceInfoSet = SetupDiGetClassDevs(
        &GUID_DEVINTERFACE_HID,
        NULL,
        NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );

    if (deviceInfoSet == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    // Enumerate all HID devices
    for (DWORD i = 0; ; i++)
    {
        if (!SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &GUID_DEVINTERFACE_HID, i, &interfaceData))
        {
            break;
        }

        // Get required buffer size
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &interfaceData, NULL, 0, &requiredSize, NULL);

        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            continue;
        }

        detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(requiredSize);
        if (!detailData)
        {
            continue;
        }
        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        if (!SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &interfaceData, detailData, requiredSize, NULL, NULL))
        {
            free(detailData);
            continue;
        }

        // Open device to check attributes
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

        // Check VID/PID
        HIDD_ATTRIBUTES attrs;
        attrs.Size = sizeof(HIDD_ATTRIBUTES);

        if (HidD_GetAttributes(hDevice, &attrs))
        {
            if (attrs.VendorID == DUALSHENSE_VID && attrs.ProductID == DUALSHENSE_PID)
            {
                // Found a DualSense!
                device.Handle = hDevice;
                device.Vid = attrs.VendorID;
                device.Pid = attrs.ProductID;
                wcsncpy_s(device.DevicePath, detailData->DevicePath, _TRUNCATE);

                // Get serial
                GetSerialString(hDevice, device.Serial, 64);

                // Determine if BT by checking device path for Bluetooth keywords
                device.IsBluetooth = (wcsstr(detailData->DevicePath, L"BTH") != NULL) ||
                                     (wcsstr(detailData->DevicePath, L"Bluetooth") != NULL) ||
                                     (wcsstr(detailData->DevicePath, L"bt") != NULL);

                found = true;
                free(detailData);
                break;
            }
        }

        CloseHandle(hDevice);
        free(detailData);
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    if (!found)
    {
        device.Handle = INVALID_HANDLE_VALUE;
    }

    return found;
}

bool HIDApi::ReadInput(HANDLE handle, BYTE* buffer, DWORD size, DWORD timeoutMs)
{
    if (handle == INVALID_HANDLE_VALUE || handle == NULL)
    {
        return false;
    }

    OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent)
    {
        return false;
    }

    DWORD bytesRead = 0;
    bool success = false;

    if (ReadFile(handle, buffer, size, &bytesRead, &ov))
    {
        success = (bytesRead == size);
    }
    else
    {
        if (GetLastError() == ERROR_IO_PENDING)
        {
            // Wait for completion or timeout
            DWORD waitResult = WaitForSingleObject(ov.hEvent, timeoutMs);
            if (waitResult == WAIT_OBJECT_0)
            {
                if (GetOverlappedResult(handle, &ov, &bytesRead, FALSE))
                {
                    success = (bytesRead == size);
                }
            }
            else
            {
                // Timeout - cancel the I/O
                CancelIo(handle);
            }
        }
    }

    CloseHandle(ov.hEvent);
    return success;
}

bool HIDApi::WriteOutput(HANDLE handle, const BYTE* buffer, DWORD size)
{
    if (handle == INVALID_HANDLE_VALUE || handle == NULL)
    {
        return false;
    }

    // For Bluetooth DualSense, use HidD_SetOutputReport
    // For USB DualSense, use WriteFile
    // We try HidD_SetOutputReport first (works for both)
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
