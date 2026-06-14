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
    m_btDevice.InputReportByteLength = 0;
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
        LOG_INFO(SVC_BT, 200, "BT DualSense found: VID=0x%04X PID=0x%04X report=%uB BT=%s Serial=%S",
            device.Vid, device.Pid, device.InputReportByteLength,
            device.IsBluetooth ? "yes" : "no", device.Serial);
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

    BYTE rawInput[78];
    BYTE usbInput[64];
    BYTE usbOutput[48];
    BYTE btOutput[78];
    DWORD btOutputSize = 0;
    DWORD inputReportSize = m_btDevice.InputReportByteLength;
    bool isBtFormat = (inputReportSize == BT_INPUT_REPORT_SIZE);
    LOG_INFO(SVC_CONV, 502, "DualSense connection type: %s, input report size: %d",
        isBtFormat ? "BT Native" : "USB Proxy", inputReportSize);

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
            inputReportSize = m_btDevice.InputReportByteLength;
            isBtFormat = (inputReportSize == BT_INPUT_REPORT_SIZE);
            LOG_INFO(SVC_CONV, 503, "Reconnected, input report size: %d", inputReportSize);
        }

        // 1. Read input report (open fresh handle each time for reliability)
        ZeroMemory(rawInput, sizeof(rawInput));
        if (!HIDApi::ReadInputFromPath(m_btDevice.DevicePath, rawInput, inputReportSize, 100))
        {
            DWORD err = GetLastError();
            if (err == ERROR_DEVICE_NOT_CONNECTED || err == ERROR_FILE_NOT_FOUND)
            {
                LOG_WARN(SVC_BT, 202, "BT controller disconnected");
                HIDApi::Close(m_btDevice.Handle);
                continue;
            }
            if (err != ERROR_TIMEOUT)
            {
                LOG_DEBUG(SVC_BT, 205, "ReadInput failed, err=%d", err);
            }
        }
        else
        {
            if (isBtFormat)
            {
                // 78-byte native BT format — remap bytes to USB layout.
                // BT layout (byte 5+ differs from USB):
                //   BT[5] = D-pad(lo) + face buttons(hi)   — same bit positions as USB
                //   BT[6] = L1,R1,L2,R2,Create,Options,L3,R3
                //   BT[7] = PS,Touchpad,Mic,padding
                //   BT[8] = L2 analog
                //   BT[9] = R2 analog
                // USB layout:
                //   USB[5] = L2 analog   USB[8] = D-pad(lo) + face buttons(hi)
                //   USB[6] = R2 analog   USB[9] = Buttons2   USB[10] = Buttons3
                ZeroMemory(usbInput, sizeof(usbInput));
                usbInput[0] = rawInput[0];             // Report ID
                usbInput[1] = rawInput[1];             // LeftX
                usbInput[2] = rawInput[2];             // LeftY
                usbInput[3] = rawInput[3];             // RightX
                usbInput[4] = rawInput[4];             // RightY
                usbInput[5] = rawInput[8];             // L2 analog (BT byte 8 → USB byte 5)
                usbInput[6] = rawInput[9];             // R2 analog (BT byte 9 → USB byte 6)
                usbInput[7] = (BYTE)(++m_sequenceCounter & 0xFF);
                usbInput[8] = rawInput[5];             // D-pad + face buttons — same bit layout!
                usbInput[9] = rawInput[6];             // L1,R1,L2-click,R2-click,Create,Options,L3,R3
                usbInput[10] = rawInput[7];            // PS,Touchpad,Mic
            }
            else
            {
                // 64-byte USB proxy — data already in USB format
                ZeroMemory(usbInput, sizeof(usbInput));
                CopyMemory(usbInput, rawInput, min(inputReportSize, sizeof(usbInput)));
                usbInput[7] = (BYTE)(++m_sequenceCounter & 0xFF);
            }

            if (m_active)
            {
                if (!SubmitInputReport(usbInput, sizeof(usbInput)))
                {
                    LOG_ERROR(SVC_IOCTL, 402, "Failed to submit input report");
                }
                else
                {
                    static int heartbeat = 0;
                    if (++heartbeat % 600 == 0)
                    {
                        LOG_DEBUG(SVC_CONV, 521, "Heartbeat: l2=%d r2=%d dpt=%02X seq=%d",
                            usbInput[5], usbInput[6], usbInput[8], m_sequenceCounter);
                    }
                }
            }
        }

        // 3. Check for output reports (haptics, LEDs from games)
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
