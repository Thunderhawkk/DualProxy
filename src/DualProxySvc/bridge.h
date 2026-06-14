#pragma once

#include <windows.h>
#include <winioctl.h>
#include "hidapi.h"
#include "report.h"
#include "logging.h"

#define SIDEBAND_DEVICE_PATH L"\\\\.\\VirtualDualSense0"

// CTL_CODE values must match kernel driver.h:
//   CTL_CODE(0x8601, Function, METHOD_BUFFERED, FILE_ANY_ACCESS)
//   = ((0x8601) << 16) | (FILE_ANY_ACCESS << 14) | (Function << 2) | METHOD_BUFFERED
//   = 0x8601A000 + (Function << 2)

#define VDS_ACTIVATE         ((DWORD)CTL_CODE(0x8601, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define VDS_DEACTIVATE       ((DWORD)CTL_CODE(0x8601, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define VDS_SUBMIT_INPUT     ((DWORD)CTL_CODE(0x8601, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define VDS_READ_OUTPUT      ((DWORD)CTL_CODE(0x8601, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define VDS_GET_OUTPUT_COUNT ((DWORD)CTL_CODE(0x8601, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS))

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
