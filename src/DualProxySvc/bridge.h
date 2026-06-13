#pragma once

#include <windows.h>
#include "hidapi.h"
#include "report.h"
#include "logging.h"

#define SIDEBAND_DEVICE_PATH L"\\\\.\\VirtualDualSense0"
#define VDS_ACTIVATE         CTL_CODE(0x8601, 0x800, 0, 0)
#define VDS_DEACTIVATE       CTL_CODE(0x8601, 0x801, 0, 0)
#define VDS_SUBMIT_INPUT     CTL_CODE(0x8601, 0x802, 0, 0)
#define VDS_READ_OUTPUT      CTL_CODE(0x8601, 0x803, 0, 0)
#define VDS_GET_OUTPUT_COUNT CTL_CODE(0x8601, 0x804, 0, 0)

#define CTL_CODE(DeviceType, Function, Method, Access) \
    (((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))

class DualSenseBridge {
public:
    DualSenseBridge();
    ~DualSenseBridge();

    bool Initialize();
    void Run();
    void Stop();
    bool Activate();
    bool Deactivate();
    bool IsActive() const { return m_active; }
    bool IsBtConnected() const { return m_btConnected; }

private:
    bool OpenSideband();
    void CloseSideband();
    bool FindBtController();
    bool SubmitInputReport(const BYTE* report, DWORD size);
    bool ReadOutputReport(BYTE* report, DWORD* size);
    bool ForwardOutputToBT(const BYTE* report, DWORD size);
    int  GetOutputReportCount();

    HANDLE m_sidebandHandle;
    DualSenseDevice m_btDevice;
    HANDLE m_stopEvent;
    bool m_active;
    bool m_btConnected;
    int  m_sequenceCounter;
};
