#include <windows.h>
#include <shellapi.h>
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

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void CreateTrayIcon(HWND hwnd);
void UpdateTrayIcon(int state);
void ShowContextMenu(HWND hwnd);
bool SendServiceCommand(const wchar_t* command, wchar_t* response, DWORD responseSize);
void ToggleEmulation();
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
        case ID_TRAY_SETTINGS:
            // Settings dialog placeholder
            MessageBoxW(hwnd, L"Settings dialog not yet implemented.", L"DualProxy", MB_OK);
            break;
        case ID_TRAY_EXIT:
            DestroyWindow(hwnd);
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
    switch (state)
    {
    case TRAY_GREEN:
        g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wcscpy_s(g_nid.szTip, L"DualProxy - Active");
        break;
    case TRAY_RED:
        g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wcscpy_s(g_nid.szTip, L"DualProxy - Inactive");
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
    HMENU menu = CreatePopupMenu();

    if (g_enabled)
    {
        AppendMenuW(menu, MF_STRING, ID_TRAY_DISABLE, L"Disable Emulation");
    }
    else
    {
        AppendMenuW(menu, MF_STRING, ID_TRAY_ENABLE, L"Enable Emulation");
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_TRAY_SETTINGS, L"Settings");
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

void ToggleEmulation()
{
    wchar_t response[64] = { 0 };

    if (g_enabled)
    {
        if (SendServiceCommand(L"DISABLE", response, sizeof(response)))
        {
            g_enabled = false;
            UpdateTrayIcon(TRAY_RED);
        }
    }
    else
    {
        if (SendServiceCommand(L"ENABLE", response, sizeof(response)))
        {
            g_enabled = true;
            UpdateTrayIcon(TRAY_GREEN);
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
