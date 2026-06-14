#include "bridge.h"
#include "logging.h"
#include <memory.h>

#define SVC_MAIN "SVC_MAIN"
#define SVC_BT   "SVC_BT"
#define SVC_IOCTL "SVC_IOCTL"
#define SVC_CONV "SVC_CONV"
#define SVC_VHF  "SVC_VHF"

DualSenseBridge::DualSenseBridge()
    : m_sidebandHandle(INVALID_HANDLE_VALUE)
    , m_active(false)
    , m_btConnected(false)
    , m_sequenceCounter(0)
    , m_lastActivateError(0)
{
    m_btDevice.Handle = INVALID_HANDLE_VALUE;
    m_btDevice.Vid = 0;
    m_btDevice.Pid = 0;
    m_btDevice.IsBluetooth = false;
    m_btDevice.DevicePath[0] = L'\0';
    m_btDevice.Serial[0] = L'\0';

    m_stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
}

DualSenseBridge::~DualSenseBridge()
{
    Stop();
    if (m_stopEvent)
    {
        CloseHandle(m_stopEvent);
    }
}

bool DualSenseBridge::Initialize()
{
    LOG_INFO(SVC_MAIN, 1, "Bridge initializing");

    if (!OpenSideband())
    {
        LOG_ERROR(SVC_IOCTL, 50, "Failed to open sideband device, error=%lu", GetLastError());
        return false;
    }

    if (!FindBtController())
    {
        LOG_WARN(SVC_BT, 51, "No BT DualSense found at init. Will retry in loop.");
    }

    return true;
}

bool DualSenseBridge::OpenSideband()
{
    if (m_sidebandHandle != INVALID_HANDLE_VALUE)
    {
        CloseSideband();
    }

    m_sidebandHandle = CreateFile(
        SIDEBAND_DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (m_sidebandHandle == INVALID_HANDLE_VALUE)
    {
        LOG_ERROR(SVC_IOCTL, 100, "CreateFile(%s) failed, error=%lu", "VirtualDualSense0", GetLastError());
        return false;
    }

    LOG_INFO(SVC_IOCTL, 101, "Sideband device opened successfully");
    return true;
}

void DualSenseBridge::CloseSideband()
{
    if (m_sidebandHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_sidebandHandle);
        m_sidebandHandle = INVALID_HANDLE_VALUE;
        LOG_INFO(SVC_IOCTL, 102, "Sideband device closed");
    }
}

bool DualSenseBridge::FindBtController()
{
    DualSenseDevice device;
    if (HIDApi::FindDualSense(device))
    {
        if (m_btDevice.Handle != INVALID_HANDLE_VALUE)
        {
            HIDApi::Close(m_btDevice.Handle);
        }

        m_btDevice = device;
        m_btConnected = true;
        LOG_INFO(SVC_BT, 200, "BT DualSense found: VID=0x%04X PID=0x%04X BT=%s Serial=%S",
            device.Vid, device.Pid, device.IsBluetooth ? "yes" : "no", device.Serial);
        return true;
    }

    m_btConnected = false;
    LOG_WARN(SVC_BT, 201, "No BT DualSense found");
    return false;
}

bool DualSenseBridge::Ping()
{
    DWORD bytesReturned;
    BOOL ok = DeviceIoControl(
        m_sidebandHandle,
        VDS_PING,
        NULL, 0UL,
        NULL, 0UL,
        &bytesReturned,
        NULL
    );
    if (!ok)
    {
        LOG_ERROR(SVC_IOCTL, 310, "VDS_PING failed, error=%lu", GetLastError());
        return false;
    }
    LOG_INFO(SVC_IOCTL, 311, "VDS_PING succeeded");
    return true;
}

bool DualSenseBridge::Activate()
{
    if (m_active)
    {
        return true;
    }

    LOG_DEBUG(SVC_VHF, 300, "Sending VDS_ACTIVATE IOCTL: 0x%08X", VDS_ACTIVATE);

    DWORD bytesReturned;
    BOOL ok = DeviceIoControl(
        m_sidebandHandle,
        VDS_ACTIVATE,
        NULL, 0UL,
        NULL, 0UL,
        &bytesReturned,
        NULL
    );

    if (!ok)
    {
        m_lastActivateError = GetLastError();
        LOG_ERROR(SVC_VHF, 300, "VDS_ACTIVATE failed, error=%lu", m_lastActivateError);
        return false;
    }

    m_active = true;
    LOG_INFO(SVC_VHF, 301, "VHF activated - virtual DualSense should now appear");
    return true;
}

bool DualSenseBridge::Deactivate()
{
    if (!m_active)
    {
        return true;
    }

    DWORD bytesReturned;
    BOOL ok = DeviceIoControl(
        m_sidebandHandle,
        VDS_DEACTIVATE,
        NULL, 0UL,
        NULL, 0UL,
        &bytesReturned,
        NULL
    );

    if (!ok)
    {
        LOG_ERROR(SVC_VHF, 302, "VDS_DEACTIVATE failed, error=%lu", GetLastError());
        return false;
    }

    m_active = false;
    LOG_INFO(SVC_VHF, 303, "VHF deactivated - virtual DualSense removed");
    return true;
}

bool DualSenseBridge::SubmitInputReport(const BYTE* report, DWORD size)
{
    DWORD bytesReturned;
    BOOL ok = DeviceIoControl(
        m_sidebandHandle,
        VDS_SUBMIT_INPUT,
        const_cast<LPVOID>(static_cast<const void*>(report)), size,
        NULL, 0UL,
        &bytesReturned,
        NULL
    );

    if (!ok)
    {
        LOG_ERROR(SVC_IOCTL, 400, "VDS_SUBMIT_INPUT failed, error=%lu", GetLastError());
        return false;
    }

    return true;
}

bool DualSenseBridge::ReadOutputReport(BYTE* report, DWORD* size)
{
    DWORD bytesReturned;
    BOOL ok = DeviceIoControl(
        m_sidebandHandle,
        VDS_READ_OUTPUT,
        NULL, 0UL,
        report, *size,
        &bytesReturned,
        NULL
    );

    if (!ok)
    {
        DWORD err = GetLastError();
        if (err == ERROR_NOT_READY)
        {
            // No output pending - this is normal
            *size = 0;
            return false;
        }
        LOG_ERROR(SVC_IOCTL, 401, "VDS_READ_OUTPUT failed, error=%d", err);
        *size = 0;
        return false;
    }

    *size = bytesReturned;
    return (bytesReturned > 0);
}

bool DualSenseBridge::ForwardOutputToBT(const BYTE* report, DWORD size)
{
    if (m_btDevice.Handle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    return HIDApi::WriteOutput(m_btDevice.Handle, report, size);
}

int DualSenseBridge::GetOutputReportCount()
{
    BYTE countBuf[4] = { 0 };
    DWORD bytesReturned;
    BOOL ok = DeviceIoControl(
        m_sidebandHandle,
        VDS_GET_OUTPUT_COUNT,
        NULL, 0UL,
        countBuf, 4,
        &bytesReturned,
        NULL
    );

    if (ok && bytesReturned == 4)
    {
        return *(int*)countBuf;
    }
    return -1;
}

void DualSenseBridge::Run()
{
    LOG_INFO(SVC_MAIN, 2, "Bridge main loop started");

    // DEBUG: Test IOCTL path with PING
    if (Ping())
    {
        LOG_INFO(SVC_IOCTL, 312, "PING OK - IOCTL handler reachable");
    }
    else
    {
        LOG_CRITICAL(SVC_IOCTL, 313, "PING FAILED - IOCTL handler NOT reachable");
        return;
    }

    // Activate VHF
    if (!Activate())
    {
        LOG_CRITICAL(SVC_VHF, 305, "Failed to activate VHF, cannot start bridge");
        return;
    }

    BYTE btInput[78];
    BYTE usbInput[64];
    BYTE usbOutput[48];
    BYTE btOutput[78];
    DWORD btOutputSize = 0;

    while (WaitForSingleObject(m_stopEvent, 0) == WAIT_TIMEOUT)
    {
        // Ensure BT controller is connected
        if (m_btDevice.Handle == INVALID_HANDLE_VALUE)
        {
            if (!FindBtController())
            {
                Sleep(1000);
                continue;
            }
        }

        // 1. Read BT input report
        ZeroMemory(btInput, sizeof(btInput));
        if (!HIDApi::ReadInput(m_btDevice.Handle, btInput, sizeof(btInput), 100))
        {
            DWORD err = GetLastError();
            if (err == ERROR_DEVICE_NOT_CONNECTED || err == ERROR_FILE_NOT_FOUND)
            {
                LOG_WARN(SVC_BT, 202, "BT controller disconnected");
                HIDApi::Close(m_btDevice.Handle);
                continue;
            }
            // Normal timeout - no new input, still alive
            // Don't log every timeout to avoid log spam
        }
        else
        {
            // 2. Convert BT to USB format
            ZeroMemory(usbInput, sizeof(usbInput));
            if (BtToUsbInputReport(btInput, sizeof(btInput), usbInput, sizeof(usbInput)))
            {
                // Set sequence number
                usbInput[7] = (BYTE)(++m_sequenceCounter & 0xFF);

                // 3. Submit to virtual controller
                if (m_active)
                {
                    if (!SubmitInputReport(usbInput, sizeof(usbInput)))
                    {
                        LOG_ERROR(SVC_IOCTL, 402, "Failed to submit input report");
                    }
                }
            }
            else
            {
                LOG_WARN(SVC_CONV, 500, "Failed to convert BT input report, size=%d", sizeof(btInput));
            }
        }

        // 4. Check for output reports (haptics, LEDs from games)
        if (m_active)
        {
            ZeroMemory(usbOutput, sizeof(usbOutput));
            DWORD outputSize = sizeof(usbOutput);
            if (ReadOutputReport(usbOutput, &outputSize) && outputSize > 0)
            {
                LOG_DEBUG(SVC_IOCTL, 403, "Output report received from VHF, size=%d", outputSize);

                // 5. Convert USB to BT and write to real controller
                ZeroMemory(btOutput, sizeof(btOutput));
                btOutputSize = sizeof(btOutput);

                if (UsbToBtOutputReport(usbOutput, outputSize, btOutput, sizeof(btOutput), &btOutputSize))
                {
                    if (!ForwardOutputToBT(btOutput, btOutputSize))
                    {
                        // Try writing to BT handle again - it might be stale
                        LOG_ERROR(SVC_BT, 203, "Failed to write output to BT controller, error=%d",
                            GetLastError());
                    }
                    else
                    {
                        LOG_DEBUG(SVC_BT, 204, "Output forwarded to BT DualSense, size=%d", btOutputSize);
                    }
                }
                else
                {
                    LOG_WARN(SVC_CONV, 501, "Failed to convert USB output report to BT format");
                }
            }
        }

        // Small yield to avoid saturating CPU
        Sleep(1);
    }

    // Cleanup
    Deactivate();
    if (m_btDevice.Handle != INVALID_HANDLE_VALUE)
    {
        HIDApi::Close(m_btDevice.Handle);
    }

    LOG_INFO(SVC_MAIN, 3, "Bridge main loop ended");
}

void DualSenseBridge::Stop()
{
    LOG_INFO(SVC_MAIN, 4, "Bridge stop requested");
    SetEvent(m_stopEvent);
}
