#include <windows.h>
#include <stdio.h>
#include "bridge.h"
#include "logging.h"

#define SERVICE_NAME L"DualProxySvc"
#define PIPE_NAME L"\\\\.\\pipe\\DualProxySvc"
#define SVC_MAIN "SVC_MAIN"
#define SVC_IPC "SVC_IPC"
#define SVC_BT   "SVC_BT"

SERVICE_STATUS_HANDLE g_statusHandle = NULL;
SERVICE_STATUS g_status = { 0 };
DualSenseBridge g_bridge;
HANDLE g_serviceStopEvent = NULL;
HANDLE g_pipeThread = NULL;
HANDLE g_bridgeThread = NULL;

BOOL IsConsoleMode(int argc, wchar_t* argv[]);
void WINAPI ServiceMain(DWORD argc, LPTSTR* argv);
void WINAPI ServiceCtrlHandler(DWORD ctrl);
void RunAsConsole();
void RunAsService();
void ReportServiceStatus(DWORD state, DWORD exitCode, DWORD waitHint);
DWORD WINAPI PipeServerThread(LPVOID lpParam);
DWORD WINAPI BridgeThread(LPVOID lpParam);

int wmain(int argc, wchar_t* argv[])
{
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

    // Start pipe server thread
    g_pipeThread = CreateThread(NULL, 0, PipeServerThread, NULL, 0, NULL);

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
        LOG_CRITICAL(SVC_MAIN, 20, "StartServiceCtrlDispatcher failed, error=%lu", GetLastError());
    }
}

void WINAPI ServiceMain(DWORD argc, LPTSTR* argv)
{
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    LOG_INFO(SVC_MAIN, 30, "ServiceMain entered");

    g_statusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_statusHandle)
    {
        LOG_CRITICAL(SVC_MAIN, 31, "RegisterServiceCtrlHandler failed, error=%lu", GetLastError());
        return;
    }

    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwServiceSpecificExitCode = 0;
    ReportServiceStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    g_serviceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_serviceStopEvent)
    {
        ReportServiceStatus(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    if (!g_bridge.Initialize())
    {
        LOG_CRITICAL(SVC_MAIN, 32, "Bridge initialization failed in service mode");
        ReportServiceStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
        return;
    }

    // Start pipe server thread for tray IPC
    g_pipeThread = CreateThread(NULL, 0, PipeServerThread, NULL, 0, NULL);
    if (!g_pipeThread)
    {
        LOG_CRITICAL(SVC_MAIN, 36, "Failed to create pipe server thread, error=%lu", GetLastError());
        ReportServiceStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
        return;
    }

    // Run bridge on a separate thread so service control handler can respond
    g_bridgeThread = CreateThread(NULL, 0, BridgeThread, NULL, 0, NULL);
    if (!g_bridgeThread)
    {
        LOG_CRITICAL(SVC_MAIN, 37, "Failed to create bridge thread, error=%lu", GetLastError());
        ReportServiceStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
        return;
    }

    // Wait for bridge to activate (or fail)
    for (int i = 0; i < 50; i++) // 5 second timeout
    {
        if (g_bridge.IsActive())
        {
            break;
        }
        if (WaitForSingleObject(g_bridgeThread, 100) == WAIT_OBJECT_0)
        {
            LOG_CRITICAL(SVC_MAIN, 38, "Bridge thread exited before activation");
            ReportServiceStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
            return;
        }
    }

    if (!g_bridge.IsActive())
    {
        LOG_CRITICAL(SVC_MAIN, 39, "Bridge activation timeout");
        ReportServiceStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
        return;
    }

    ReportServiceStatus(SERVICE_RUNNING, NO_ERROR, 0);

    // Wait for bridge to finish
    WaitForSingleObject(g_bridgeThread, INFINITE);
    CloseHandle(g_bridgeThread);
    g_bridgeThread = NULL;

    // Signal pipe server to stop
    if (g_pipeThread)
    {
        WaitForSingleObject(g_pipeThread, 5000);
        CloseHandle(g_pipeThread);
        g_pipeThread = NULL;
    }

    LOG_INFO(SVC_MAIN, 34, "Bridge loop ended, stopping service");
    CloseHandle(g_serviceStopEvent);
    ReportServiceStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

DWORD WINAPI BridgeThread(LPVOID lpParam)
{
    UNREFERENCED_PARAMETER(lpParam);
    LOG_INFO(SVC_MAIN, 33, "Service running, starting bridge loop on thread");
    g_bridge.Run();
    return 0;
}

static SECURITY_ATTRIBUTES g_pipeSa;
static bool g_pipeSaInitialized = false;

static SECURITY_ATTRIBUTES* GetPipeSecurityAttributes()
{
    if (!g_pipeSaInitialized)
    {
        // Create a DACL granting Everyone GENERIC_READ | GENERIC_WRITE.
        // Without this, pipes created by LocalSystem may reject non-admin clients.
        PSECURITY_DESCRIPTOR sd = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
        if (sd)
        {
            InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION);
            SetSecurityDescriptorDacl(sd, TRUE, NULL, FALSE);
            g_pipeSa.nLength = sizeof(SECURITY_ATTRIBUTES);
            g_pipeSa.lpSecurityDescriptor = sd;
            g_pipeSa.bInheritHandle = FALSE;
        }
        g_pipeSaInitialized = true;
    }
    return &g_pipeSa;
}

DWORD WINAPI PipeServerThread(LPVOID lpParam)
{
    UNREFERENCED_PARAMETER(lpParam);

    LOG_INFO(SVC_IPC, 501, "Named pipe server thread started");

    HANDLE stopEvent = g_serviceStopEvent;

    for (;;)
    {
        // Check for stop signal
        if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
        {
            break;
        }

        HANDLE pipe = CreateNamedPipeW(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            512,
            512,
            0,
            GetPipeSecurityAttributes()
        );

        if (pipe == INVALID_HANDLE_VALUE)
        {
            LOG_ERROR(SVC_IPC, 502, "CreateNamedPipe failed, error=%lu", GetLastError());
            Sleep(1000);
            continue;
        }

        // Async connect so we can abort on stop signal
        OVERLAPPED ov = { 0 };
        ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (!ov.hEvent)
        {
            CloseHandle(pipe);
            continue;
        }

        BOOL connected = ConnectNamedPipe(pipe, &ov);
        if (!connected && GetLastError() == ERROR_IO_PENDING)
        {
            HANDLE events[2] = { stopEvent, ov.hEvent };
            DWORD waitResult = WaitForMultipleObjects(2, events, FALSE, INFINITE);
            if (waitResult == WAIT_OBJECT_0)
            {
                CancelIo(pipe);
                CloseHandle(ov.hEvent);
                CloseHandle(pipe);
                break;
            }
            DWORD dummy;
            GetOverlappedResult(pipe, &ov, &dummy, FALSE);
            connected = TRUE;
        }
        else if (!connected && GetLastError() == ERROR_PIPE_CONNECTED)
        {
            connected = TRUE;
        }

        CloseHandle(ov.hEvent);

        if (!connected)
        {
            LOG_WARN(SVC_IPC, 503, "ConnectNamedPipe failed, error=%lu", GetLastError());
            CloseHandle(pipe);
            continue;
        }

        // Read command
        wchar_t command[64] = { 0 };
        DWORD bytesRead;
        BOOL ok = ReadFile(pipe, command, sizeof(command) - sizeof(wchar_t), &bytesRead, NULL);
        if (!ok)
        {
            CloseHandle(pipe);
            continue;
        }
        command[bytesRead / sizeof(wchar_t)] = L'\0';

        LOG_DEBUG(SVC_IPC, 504, "Pipe command received: %S", command);

        // Process command
        wchar_t response[64] = { 0 };

        if (wcscmp(command, L"ENABLE") == 0)
        {
            bool emuOk = g_bridge.Activate();
            g_bridge.EnableHidHide();
            if (emuOk)
            {
                wcscpy_s(response, L"OK");
                LOG_INFO(SVC_IPC, 505, "ENABLE command processed successfully");
            }
            else
            {
                wcscpy_s(response, L"FAIL");
                LOG_WARN(SVC_IPC, 506, "ENABLE command failed");
            }
        }
        else if (wcscmp(command, L"DISABLE") == 0)
        {
            bool emuOk = g_bridge.Deactivate();
            g_bridge.DisableHidHide();
            if (emuOk)
            {
                wcscpy_s(response, L"OK");
                LOG_INFO(SVC_IPC, 507, "DISABLE command processed successfully");
            }
            else
            {
                wcscpy_s(response, L"FAIL");
                LOG_WARN(SVC_IPC, 508, "DISABLE command failed");
            }
        }
        else if (wcscmp(command, L"STATUS") == 0)
        {
            wchar_t emuState[32];
            if (g_bridge.IsBtConnected())
            {
                wcscpy_s(emuState, g_bridge.IsActive() ? L"active" : L"inactive");
            }
            else
            {
                wcscpy_s(emuState, L"disconnected");
            }
            swprintf_s(response, L"%s|%s", emuState,
                g_bridge.IsHidHideActive() ? L"hiding_on" : L"hiding_off");
        }
        else if (wcscmp(command, L"HIDHIDE_ON") == 0)
        {
            if (g_bridge.EnableHidHide())
            {
                wcscpy_s(response, L"OK");
                LOG_INFO(SVC_IPC, 510, "HIDHIDE_ON processed");
            }
            else
            {
                wcscpy_s(response, L"FAIL");
                LOG_WARN(SVC_IPC, 511, "HIDHIDE_ON failed");
            }
        }
        else if (wcscmp(command, L"HIDHIDE_OFF") == 0)
        {
            if (g_bridge.DisableHidHide())
            {
                wcscpy_s(response, L"OK");
                LOG_INFO(SVC_IPC, 512, "HIDHIDE_OFF processed");
            }
            else
            {
                wcscpy_s(response, L"FAIL");
                LOG_WARN(SVC_IPC, 513, "HIDHIDE_OFF failed");
            }
        }
        else
        {
            wcscpy_s(response, L"UNKNOWN");
            LOG_WARN(SVC_IPC, 509, "Unknown command: %S", command);
        }

        // Send response
        DWORD bytesWritten;
        WriteFile(pipe, response, (DWORD)((wcslen(response) + 1) * sizeof(wchar_t)), &bytesWritten, NULL);

        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    return 0;
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
