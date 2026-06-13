#pragma once

#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <cfgmgr32.h>
#include <string>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

struct DualSenseDevice {
    HANDLE Handle;
    wchar_t DevicePath[256];
    wchar_t Serial[64];
    USHORT Vid;
    USHORT Pid;
    bool IsBluetooth;
};

class HIDApi {
public:
    // Enumerate and find the first connected DualSense (BT preferred)
    static bool FindDualSense(DualSenseDevice& device);

    // Read input report (blocking with timeout)
    static bool ReadInput(HANDLE handle, BYTE* buffer, DWORD size, DWORD timeoutMs = 100);

    // Write output report
    static bool WriteOutput(HANDLE handle, const BYTE* buffer, DWORD size);

    // Send feature report
    static bool SetFeature(HANDLE handle, const BYTE* buffer, DWORD size);

    // Get feature report
    static bool GetFeature(HANDLE handle, BYTE* buffer, DWORD size);

    // Close device handle
    static void Close(HANDLE& handle);

private:
    static bool GetDeviceAttributes(HANDLE handle, USHORT& vid, USHORT& pid);
    static bool GetManufacturerString(HANDLE handle, wchar_t* buffer, ULONG size);
    static bool GetProductString(HANDLE handle, wchar_t* buffer, ULONG size);
    static bool GetSerialString(HANDLE handle, wchar_t* buffer, ULONG size);
};
