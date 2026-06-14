#include "logging.h"
#include <io.h>

Logger& Logger::Instance()
{
    static Logger instance;
    return instance;
}

Logger::Logger()
    : m_minLevel(LOG_DEBUG)
    , m_initialized(false)
{
    InitializeCriticalSection(&m_cs);
    m_component[0] = L'\0';
    m_logPath[0] = L'\0';
}

Logger::~Logger()
{
    Shutdown();
    DeleteCriticalSection(&m_cs);
}

void Logger::Init(const wchar_t* componentName, LogLevel minLevel)
{
    EnterCriticalSection(&m_cs);

    wcsncpy_s(m_component, componentName, _TRUNCATE);
    m_minLevel = minLevel;

    // Create log directory: PROGRAMDATA\DualProxy\logs
    wchar_t progData[MAX_PATH];
    if (GetEnvironmentVariableW(L"PROGRAMDATA", progData, MAX_PATH) > 0)
    {
        swprintf_s(m_logPath, L"%s\\DualProxy\\logs\\%s.log", progData, componentName);

        wchar_t dirPath[MAX_PATH];
        swprintf_s(dirPath, L"%s\\DualProxy\\logs", progData);
        wchar_t dualProxyDir[MAX_PATH];
        swprintf_s(dualProxyDir, L"%s\\DualProxy", progData);
        CreateDirectoryW(dualProxyDir, NULL);
        CreateDirectoryW(dirPath, NULL);
    }
    else
    {
        wcscpy_s(m_logPath, L"DualProxy.log");
    }

    m_initialized = true;

    // Log initialization (write directly, no recursion)
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char timeBuf[64];
        sprintf_s(timeBuf, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

        FILE* f = NULL;
        if (_wfopen_s(&f, m_logPath, L"a") == 0 && f)
        {
            fprintf(f, "[%s] [%S-000] [INIT] [LOGGER] Logger initialized for component '%S'\n",
                timeBuf, m_component, m_component);
            fclose(f);
        }
    }

    LeaveCriticalSection(&m_cs);
}

void Logger::Shutdown()
{
    EnterCriticalSection(&m_cs);
    m_initialized = false;
    LeaveCriticalSection(&m_cs);
}

void Logger::SetMinLevel(LogLevel level)
{
    EnterCriticalSection(&m_cs);
    m_minLevel = level;
    LeaveCriticalSection(&m_cs);
}

const char* Logger::LevelToString(LogLevel level)
{
    switch (level)
    {
    case LOG_DEBUG:    return "DEBUG";
    case LOG_INFO:     return "INFO";
    case LOG_WARN:     return "WARN";
    case LOG_ERROR:    return "ERROR";
    case LOG_CRITICAL: return "CRITICAL";
    default:           return "UNKNOWN";
    }
}

void Logger::Log(LogLevel level, const char* category, int uniqueId, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    LogV(level, category, uniqueId, format, args);
    va_end(args);
}

void Logger::LogV(LogLevel level, const char* category, int uniqueId, const char* format, va_list args)
{
    if (!m_initialized || level < m_minLevel)
    {
        return;
    }

    char message[LOG_MAX_ENTRY];
    vsnprintf_s(message, LOG_MAX_ENTRY, _TRUNCATE, format, args);

    WriteLog(level, category, uniqueId, message);
}

void Logger::WriteLog(LogLevel level, const char* category, int uniqueId, const char* message)
{
    SYSTEMTIME st;
    GetLocalTime(&st);

    char timeBuf[32];
    sprintf_s(timeBuf, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    char logLine[LOG_MAX_ENTRY + 128];

    sprintf_s(logLine, "[%s] [%S-%03d] [%s] [%s] %s\n",
        timeBuf, m_component, uniqueId, LevelToString(level), category, message);

    EnterCriticalSection(&m_cs);

    RotateIfNeeded();

    FILE* f = NULL;
    if (_wfopen_s(&f, m_logPath, L"a") == 0 && f)
    {
        fputs(logLine, f);
        if (level >= LOG_ERROR)
        {
            fflush(f);
        }
        fclose(f);
    }

    // Also output to debug console
    OutputDebugStringA(logLine);

    LeaveCriticalSection(&m_cs);
}

void Logger::RotateIfNeeded()
{
    if (!m_initialized || m_logPath[0] == L'\0')
    {
        return;
    }

    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (!GetFileAttributesExW(m_logPath, GetFileExInfoStandard, &fileInfo))
    {
        return;
    }

    LARGE_INTEGER fileSize;
    fileSize.HighPart = fileInfo.nFileSizeHigh;
    fileSize.LowPart = fileInfo.nFileSizeLow;

    if (fileSize.QuadPart < LOG_MAX_FILE_SIZE)
    {
        return;
    }

    // Close current file (we'll reopen after rotation)
    // Rename .log -> .1.log, .1.log -> .2.log, etc.
    wchar_t oldPath[MAX_PATH];
    wchar_t newPath[MAX_PATH];

    // Remove the oldest file
    swprintf_s(oldPath, L"%s.%d.log", m_logPath, LOG_MAX_FILES - 1);
    _wremove(oldPath);

    // Shift files
    for (int i = LOG_MAX_FILES - 2; i >= 0; i--)
    {
        swprintf_s(oldPath, L"%s.%d.log", m_logPath, i);
        swprintf_s(newPath, L"%s.%d.log", m_logPath, i + 1);
        _wrename(oldPath, newPath);
    }

    // Rename current .log to .0.log
    swprintf_s(newPath, L"%s.0.log", m_logPath);
    _wrename(m_logPath, newPath);
}

void Logger::GetLogPath(wchar_t* path, size_t pathSize)
{
    EnterCriticalSection(&m_cs);
    wcsncpy_s(path, pathSize, m_logPath, _TRUNCATE);
    LeaveCriticalSection(&m_cs);
}
