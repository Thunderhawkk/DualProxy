#pragma once

#include <windows.h>
#include <winioctl.h>
#include "hidapi.h"
#include "report.h"
#include "logging.h"

#define SIDEBAND_DEVICE_PATH L"\\\\.\\VirtualDualSense0"

#define VDS_ACTIVATE         0x222000
#define VDS_DEACTIVATE       0x222004
#define VDS_SUBMIT_INPUT     0x222008
#define VDS_READ_OUTPUT      0x22200C
#define VDS_GET_OUTPUT_COUNT 0x222010
#define VDS_PING             0x222014

class DualSenseBridge {
public:
    DualSenseBridge();
    ~DualSenseBridge();

    bool Initialize();
    void Run();
    void Stop();
    bool Activate();
    bool Deactivate();
    bool Ping();
    bool IsActive() const { return m_active; }
    DWORD GetLastActivateError() const { return m_lastActivateError; }
    bool IsBtConnected() const { return m_btConnected; }

private:
    bool OpenSideband();
    void CloseSideband();
    bool FindBtController();
    bool SubmitInputReport(const BYTE* report, DWORD size);
    bool ReadOutputReport(BYTE* report, DWORD* size);
    int  GetOutputReportCount();

    HANDLE m_sidebandHandle;
    DualSenseDevice m_btDevice;        // 78-byte native BT (input source)
    DualSenseDevice m_usbDevice;       // 64-byte USB proxy (output target)
    HANDLE m_stopEvent;
    bool m_active;
    bool m_btConnected;
    int  m_sequenceCounter;
    DWORD m_lastActivateError;
};
