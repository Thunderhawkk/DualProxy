#pragma once

#include <windows.h>
#include <stdio.h>
#include <strsafe.h>
#include <stdlib.h>
#include <time.h>

#define LOG_MAX_ENTRY 512
#define LOG_MAX_FILES 5
#define LOG_MAX_FILE_SIZE (10 * 1024 * 1024)

#define LOG_DIR L"%PROGRAMDATA%\\DualProxy\\logs"

enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3,
    LOG_CRITICAL = 4
};

class Logger {
public:
    static Logger& Instance();
    void Init(const wchar_t* componentName, LogLevel minLevel = LOG_DEBUG);
    void Log(LogLevel level, const char* category, int uniqueId, const char* format, ...);
    void SetMinLevel(LogLevel level);
    void Shutdown();

private:
    Logger();
    ~Logger();
    void WriteLog(LogLevel level, const char* category, int uniqueId, const char* message);
    void RotateIfNeeded();
    void GetLogPath(wchar_t* path, size_t pathSize);
    const char* LevelToString(LogLevel level);
    void LogV(LogLevel level, const char* category, int uniqueId, const char* format, va_list args);

    CRITICAL_SECTION m_cs;
    wchar_t m_component[32];
    LogLevel m_minLevel;
    wchar_t m_logPath[MAX_PATH];
    bool m_initialized;
};

// Convenience macros
// Usage: LOG_DEBUG("BT_ENUM", 51, "Found device VID=0x%04X PID=0x%04X", vid, pid);
#define LOG_INIT(comp)           Logger::Instance().Init(comp)
#define LOG_DEBUG(cat, id, ...)  Logger::Instance().Log(LOG_DEBUG, cat, id, __VA_ARGS__)
#define LOG_INFO(cat, id, ...)   Logger::Instance().Log(LOG_INFO, cat, id, __VA_ARGS__)
#define LOG_WARN(cat, id, ...)   Logger::Instance().Log(LOG_WARN, cat, id, __VA_ARGS__)
#define LOG_ERROR(cat, id, ...)  Logger::Instance().Log(LOG_ERROR, cat, id, __VA_ARGS__)
#define LOG_CRITICAL(cat, id, ...) Logger::Instance().Log(LOG_CRITICAL, cat, id, __VA_ARGS__)

// Log category constants
// Service: SVC_MAIN, SVC_BT, SVC_VHF, SVC_IOCTL, SVC_CONV, SVC_FEAT, SVC_IPC
// Driver: DRV_INIT, DRV_PNP, DRV_VHF, DRV_IOCTL, DRV_BT
// Tray: TRAY_MAIN, TRAY_IPC, TRAY_HK, TRAY_STATE
