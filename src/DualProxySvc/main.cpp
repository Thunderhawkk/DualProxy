#include <windows.h>
#include <stdio.h>
#include "bridge.h"
#include "logging.h"

#define SERVICE_NAME L"DualProxySvc"
#define SERVICE_DISPLAY_NAME L"DualProxy DualSense Bridge Service"

SERVICE_STATUS_HANDLE g_statusHandle = NULL;
SERVICE_STATUS g_status = { 0 };
DualSenseBridge g_bridge;
HANDLE g_serviceStopEvent = NULL;

BOOL IsConsoleMode(int argc, wchar_t* argv[]);
void WINAPI ServiceMain(DWORD argc, LPTSTR* argv);
void WINAPI ServiceCtrlHandler(DWORD ctrl);
void RunAsConsole();
void RunAsService();
void ReportServiceStatus(DWORD state, DWORD exitCode, DWORD waitHint);

int wmain(int argc, wchar_t* argv[])
{
    // Initialize logging
    LOG_INIT(L"DualProxySvc");

    LOG_INFO(SVC_MAIN, 1, "DualProxySvc starting, argc=%d", argc);
    for (int i = 0; i < argc; i++)
    {
        LOG_DEBUG(SVC_MAIN, 2, "  argv[%d] = %S", i, argv[i]);
    }

    if (IsConsoleMode(argc, argv))
    {
        LOG_INFO(SVC_MAIN, 3, "Running in console mode");
        RunAsConsole();
    }
    else
    {
        LOG_INFO(SVC_MAIN, 4, "Running as Windows service");
        RunAsService();
    }

    LOG_INFO(SVC_MAIN, 5, "DualProxySvc exiting");
    Logger::Instance().Shutdown();
    return 0;
}

BOOL IsConsoleMode(int argc, wchar_t* argv[])
{
    for (int i = 1; i < argc; i++)
    {
        if (wcscmp(argv[i], L"--console") == 0 ||
            wcscmp(argv[i], L"-c") == 0)
        {
            return TRUE;
        }
    }
    return FALSE;
}

void RunAsConsole()
{
    LOG_INFO(SVC_MAIN, 10, "Console mode: initializing bridge");

    if (!g_bridge.Initialize())
    {
        LOG_CRITICAL(SVC_MAIN, 11, "Bridge initialization failed");
        return;
    }

    LOG_INFO(SVC_MAIN, 12, "Bridge initialized. Running main loop.");
    printf("DualProxySvc running in console mode. Press Ctrl+C to stop.\n");

    g_bridge.Run();

    printf("DualProxySvc stopped.\n");
}

void RunAsService()
{
    SERVICE_TABLE_ENTRY serviceTable[] = {
        { (LPWSTR)SERVICE_NAME, ServiceMain },
        { NULL, NULL }
    };

    if (!StartServiceCtrlDispatcher(serviceTable))
    {
        LOG_CRITICAL(SVC_MAIN, 20, "StartServiceCtrlDispatcher failed, error=%d", GetLastError());
    }
}

void WINAPI ServiceMain(DWORD argc, LPTSTR* argv)
{
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    LOG_INFO(SVC_MAIN, 30, "ServiceMain entered");

    // Register the service control handler
    g_statusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_statusHandle)
    {
        LOG_CRITICAL(SVC_MAIN, 31, "RegisterServiceCtrlHandler failed, error=%d", GetLastError());
        return;
    }

    // Initial status
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwServiceSpecificExitCode = 0;
    ReportServiceStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    // Create stop event
    g_serviceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_serviceStopEvent)
    {
        ReportServiceStatus(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    ReportServiceStatus(SERVICE_RUNNING, NO_ERROR, 0);

    // Initialize and run the bridge
    if (!g_bridge.Initialize())
    {
        LOG_CRITICAL(SVC_MAIN, 32, "Bridge initialization failed in service mode");
        ReportServiceStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
        return;
    }

    LOG_INFO(SVC_MAIN, 33, "Service running, starting bridge loop");
    g_bridge.Run();

    // Cleanup
    LOG_INFO(SVC_MAIN, 34, "Bridge loop ended, stopping service");
    CloseHandle(g_serviceStopEvent);
    ReportServiceStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

void WINAPI ServiceCtrlHandler(DWORD ctrl)
{
    switch (ctrl)
    {
    case SERVICE_CONTROL_STOP:
        LOG_INFO(SVC_MAIN, 40, "Service control: STOP received");
        ReportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        g_bridge.Stop();
        SetEvent(g_serviceStopEvent);
        break;

    case SERVICE_CONTROL_SHUTDOWN:
        LOG_INFO(SVC_MAIN, 41, "Service control: SHUTDOWN received");
        g_bridge.Stop();
        SetEvent(g_serviceStopEvent);
        break;

    case SERVICE_CONTROL_INTERROGATE:
        ReportServiceStatus(g_status.dwCurrentState, NO_ERROR, 0);
        break;

    default:
        LOG_DEBUG(SVC_MAIN, 42, "Service control: unknown code=%d", ctrl);
        break;
    }
}

void ReportServiceStatus(DWORD state, DWORD exitCode, DWORD waitHint)
{
    static DWORD checkpoint = 1;

    g_status.dwCurrentState = state;
    g_status.dwWin32ExitCode = exitCode;
    g_status.dwWaitHint = waitHint;

    if (state == SERVICE_START_PENDING)
    {
        g_status.dwControlsAccepted = 0;
    }
    else
    {
        g_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }

    if (state == SERVICE_RUNNING || state == SERVICE_STOPPED)
    {
        g_status.dwCheckPoint = 0;
    }
    else
    {
        g_status.dwCheckPoint = checkpoint++;
    }

    SetServiceStatus(g_statusHandle, &g_status);
}
