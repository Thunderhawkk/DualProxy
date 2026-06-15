#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <stdio.h>
#include <strsafe.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

#define WM_TRAY_ICON      (WM_APP + 1)
#define WM_TRAY_CALLBACK  (WM_APP + 2)
#define ID_TRAY_EXIT      1001
#define ID_TRAY_ENABLE    1002
#define ID_TRAY_DISABLE   1003
#define ID_TRAY_SETTINGS  1004
#define ID_TRAY_HIDHIDE_ON   1005
#define ID_TRAY_HIDHIDE_OFF  1006
#define ID_TRAY_INSTALL_DRIVER   1007
#define ID_TRAY_UNINSTALL_DRIVER 1008
#define ID_TRAY_BUILD_DRIVERS    1009
#define HOTKEY_ID         1

#define TRAY_GREEN  0
#define TRAY_RED    1
#define TRAY_YELLOW 2

#define PIPE_NAME L"\\\\.\\pipe\\DualProxySvc"

HINSTANCE g_hInst;
HWND g_hwnd;
NOTIFYICONDATAW g_nid;
int g_state = TRAY_RED;
bool g_enabled = false;
bool g_hidHideOn = false;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void CreateTrayIcon(HWND hwnd);
void UpdateTrayIcon(int state);
void ShowContextMenu(HWND hwnd);
bool SendServiceCommand(const wchar_t* command, wchar_t* response, DWORD responseSize);
void ToggleEmulation();
void ToggleHidHide();
void RefreshStates();
void LoadSettings();
void SaveSettings();

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    g_hInst = hInstance;

    // Register window class
    const wchar_t CLASS_NAME[] = L"DualProxyTrayWindow";

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc))
    {
        return 0;
    }

    // Create hidden window
    g_hwnd = CreateWindowExW(
        0, CLASS_NAME, L"DualProxy",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hwnd)
    {
        return 0;
    }

    // Register hotkey (Ctrl+Win+D)
    if (!RegisterHotKey(g_hwnd, HOTKEY_ID, MOD_CONTROL | MOD_WIN, 'D'))
    {
        // Non-fatal - hotkey might fail if already registered
    }

    // Create tray icon
    CreateTrayIcon(g_hwnd);

    // Load settings
    LoadSettings();
    RefreshStates();

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    UnregisterHotKey(g_hwnd, HOTKEY_ID);

    return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        return 0;

    case WM_TRAY_CALLBACK:
        if (lp == WM_LBUTTONUP)
        {
            ToggleEmulation();
        }
        else if (lp == WM_RBUTTONUP)
        {
            ShowContextMenu(hwnd);
        }
        else if (lp == WM_LBUTTONDBLCLK)
        {
            ToggleEmulation();
        }
        return 0;

    case WM_HOTKEY:
        if (wp == HOTKEY_ID)
        {
            ToggleEmulation();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case ID_TRAY_ENABLE:
            ToggleEmulation();
            break;
        case ID_TRAY_DISABLE:
            ToggleEmulation();
            break;
        case ID_TRAY_HIDHIDE_ON:
        case ID_TRAY_HIDHIDE_OFF:
            ToggleHidHide();
            break;
        case ID_TRAY_INSTALL_DRIVER:
        {
            WCHAR exeDir[MAX_PATH], scriptDir[MAX_PATH];
            GetModuleFileNameW(NULL, exeDir, MAX_PATH);
            PathRemoveFileSpecW(exeDir);
            wcscpy_s(scriptDir, exeDir);
            PathAppendW(scriptDir, L"scripts");
            if (GetFileAttributesW(scriptDir) == INVALID_FILE_ATTRIBUTES)
            {
                wcscpy_s(scriptDir, exeDir);
                PathAppendW(scriptDir, L"..\\..\\..\\scripts");
            }
            WCHAR params[512];
            swprintf_s(params, L"-ExecutionPolicy Bypass -NoExit -File \"install.ps1\"");
            ShellExecuteW(hwnd, L"runas", L"powershell.exe", params, scriptDir, SW_SHOW);
            break;
        }
        case ID_TRAY_UNINSTALL_DRIVER:
        {
            WCHAR exeDir[MAX_PATH], scriptDir[MAX_PATH];
            GetModuleFileNameW(NULL, exeDir, MAX_PATH);
            PathRemoveFileSpecW(exeDir);
            wcscpy_s(scriptDir, exeDir);
            PathAppendW(scriptDir, L"scripts");
            if (GetFileAttributesW(scriptDir) == INVALID_FILE_ATTRIBUTES)
            {
                wcscpy_s(scriptDir, exeDir);
                PathAppendW(scriptDir, L"..\\..\\..\\scripts");
            }
            WCHAR params[512];
            swprintf_s(params, L"-ExecutionPolicy Bypass -NoExit -File \"uninstall.ps1\"");
            ShellExecuteW(hwnd, L"runas", L"powershell.exe", params, scriptDir, SW_SHOW);
            break;
        }
        case ID_TRAY_BUILD_DRIVERS:
        {
            WCHAR srcDir[MAX_PATH];
            GetModuleFileNameW(NULL, srcDir, MAX_PATH);
            PathRemoveFileSpecW(srcDir);
            PathAppendW(srcDir, L"..\\..\\..\\src");
            WCHAR cmdLine[1024];
            swprintf_s(cmdLine,
                L"/k \"\"C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat\" >nul 2>&1 && MSBuild \"VirtualDualSense.sln\" /p:Configuration=Debug /p:Platform=x64\"");
            ShellExecuteW(hwnd, NULL, L"cmd.exe", cmdLine, srcDir, SW_SHOW);
            break;
        }
        case ID_TRAY_SETTINGS:
            MessageBoxW(hwnd, L"Settings dialog not yet implemented.", L"DualProxy", MB_OK);
            break;
        case ID_TRAY_EXIT:
            DestroyWindow(hwnd);
            break;
        default:
            break;
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}

void CreateTrayIcon(HWND hwnd)
{
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    g_nid.uCallbackMessage = WM_TRAY_CALLBACK;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"DualProxy - Virtual DualSense");

    UpdateTrayIcon(TRAY_RED);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void UpdateTrayIcon(int state)
{
    // Use standard icons for now (replace with custom icons later)
    const wchar_t* hidingSuffix = g_hidHideOn ? L" (Hiding On)" : L"";

    switch (state)
    {
    case TRAY_GREEN:
        g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        swprintf_s(g_nid.szTip, L"DualProxy - Active%s", hidingSuffix);
        break;
    case TRAY_RED:
        g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        swprintf_s(g_nid.szTip, L"DualProxy - Inactive%s", hidingSuffix);
        break;
    case TRAY_YELLOW:
        g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wcscpy_s(g_nid.szTip, L"DualProxy - No Controller");
        break;
    }

    g_state = state;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void ShowContextMenu(HWND hwnd)
{
    // Refresh state from service before showing menu
    RefreshStates();

    HMENU menu = CreatePopupMenu();

    // Status text (disabled/grayed out)
    {
        wchar_t statusBuf[128];
        swprintf_s(statusBuf, L"Emulation: %s", g_enabled ? L"Active" : L"Inactive");
        AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, statusBuf);
    }
    {
        wchar_t statusBuf[128];
        swprintf_s(statusBuf, L"Hiding: %s", g_hidHideOn ? L"On" : L"Off");
        AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, statusBuf);
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);

    if (g_enabled)
    {
        AppendMenuW(menu, MF_STRING, ID_TRAY_DISABLE, L"Disable Emulation");
    }
    else
    {
        AppendMenuW(menu, MF_STRING, ID_TRAY_ENABLE, L"Enable Emulation");
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);

    if (g_hidHideOn)
    {
        AppendMenuW(menu, MF_STRING, ID_TRAY_HIDHIDE_OFF, L"Disable Hiding");
    }
    else
    {
        AppendMenuW(menu, MF_STRING, ID_TRAY_HIDHIDE_ON, L"Enable Hiding");
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);

    // Settings submenu
    HMENU settingsMenu = CreatePopupMenu();
    AppendMenuW(settingsMenu, MF_STRING, ID_TRAY_INSTALL_DRIVER, L"Install Driver");
    AppendMenuW(settingsMenu, MF_STRING, ID_TRAY_UNINSTALL_DRIVER, L"Uninstall Driver");
    AppendMenuW(settingsMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(settingsMenu, MF_STRING, ID_TRAY_BUILD_DRIVERS, L"Build Drivers");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)settingsMenu, L"Settings");

    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    PostMessage(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

bool SendServiceCommand(const wchar_t* command, wchar_t* response, DWORD responseSize)
{
    HANDLE pipe = CreateFileW(
        PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (pipe == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    DWORD bytesWritten;
    DWORD cmdLen = (DWORD)((wcslen(command) + 1) * sizeof(wchar_t));
    BOOL ok = WriteFile(pipe, command, cmdLen, &bytesWritten, NULL);

    if (!ok)
    {
        CloseHandle(pipe);
        return false;
    }

    DWORD bytesRead;
    ok = ReadFile(pipe, response, responseSize, &bytesRead, NULL);
    CloseHandle(pipe);

    if (ok && bytesRead > 0)
    {
        response[bytesRead / sizeof(wchar_t)] = L'\0';
        return true;
    }

    return false;
}

void ShowIpcError(DWORD errorCode)
{
    wchar_t buf[256];
    swprintf_s(buf, L"Failed to communicate with DualProxy service.\nError: %lu\n\nIs the service running?", errorCode);
    MessageBoxW(g_hwnd, buf, L"DualProxy", MB_OK | MB_ICONERROR);
}

void ToggleEmulation()
{
    wchar_t response[64] = { 0 };

    if (g_enabled)
    {
        if (!SendServiceCommand(L"DISABLE", response, sizeof(response)))
        {
            ShowIpcError(GetLastError());
        }
        else if (wcscmp(response, L"OK") != 0)
        {
            MessageBoxW(g_hwnd, L"Service returned unexpected response.", L"DualProxy", MB_OK | MB_ICONERROR);
        }
        else
        {
            g_enabled = false;
            UpdateTrayIcon(TRAY_RED);
        }
    }
    else
    {
        if (!SendServiceCommand(L"ENABLE", response, sizeof(response)))
        {
            ShowIpcError(GetLastError());
        }
        else if (wcscmp(response, L"OK") != 0)
        {
            MessageBoxW(g_hwnd, L"Service returned unexpected response.", L"DualProxy", MB_OK | MB_ICONERROR);
        }
        else
        {
            g_enabled = true;
            UpdateTrayIcon(TRAY_GREEN);
        }
    }
}

void ToggleHidHide()
{
    wchar_t response[64] = { 0 };
    const wchar_t* cmd = g_hidHideOn ? L"HIDHIDE_OFF" : L"HIDHIDE_ON";

    if (!SendServiceCommand(cmd, response, sizeof(response)))
    {
        ShowIpcError(GetLastError());
    }
    else if (wcscmp(response, L"OK") != 0)
    {
        MessageBoxW(g_hwnd, L"Service returned unexpected response.", L"DualProxy", MB_OK | MB_ICONERROR);
    }
    else
    {
        g_hidHideOn = !g_hidHideOn;
        UpdateTrayIcon(g_state);
    }
}

void RefreshStates()
{
    wchar_t response[64] = { 0 };
    if (SendServiceCommand(L"STATUS", response, sizeof(response)))
    {
        // Format: "active|hiding_on" or "inactive|hiding_off" or "disconnected|hiding_off"
        wchar_t* pipe = wcschr(response, L'|');
        if (pipe)
        {
            *pipe = L'\0';
            g_hidHideOn = (wcscmp(pipe + 1, L"hiding_on") == 0);

            if (wcscmp(response, L"active") == 0)
            {
                g_enabled = true;
                UpdateTrayIcon(TRAY_GREEN);
            }
            else if (wcscmp(response, L"inactive") == 0)
            {
                g_enabled = false;
                UpdateTrayIcon(TRAY_RED);
            }
            else
            {
                g_enabled = false;
                UpdateTrayIcon(TRAY_YELLOW);
            }
        }
    }
}

void LoadSettings()
{
    wchar_t path[MAX_PATH];
    GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    wcscat_s(path, L"\\DualProxy\\settings.ini");

    // Read settings from INI file (if exists)
    g_enabled = GetPrivateProfileIntW(L"Settings", L"Enabled", 0, path) != 0;

    if (g_enabled)
    {
        UpdateTrayIcon(TRAY_GREEN);
    }
}

void SaveSettings()
{
    wchar_t path[MAX_PATH];
    GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    wcscat_s(path, L"\\DualProxy\\settings.ini");

    // Create directory if needed
    wchar_t dir[MAX_PATH];
    wcscpy_s(dir, path);
    PathRemoveFileSpecW(dir);
    CreateDirectoryW(dir, NULL);

    wchar_t buf[16];
    swprintf_s(buf, L"%d", g_enabled ? 1 : 0);
    WritePrivateProfileStringW(L"Settings", L"Enabled", buf, path);
}
