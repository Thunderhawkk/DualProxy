#pragma once

#ifdef WPP_CONTROL_GUIDS
#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID( \
        VirtualDualSenseTraceGuid, \
        (d93b1a1a,2a8c,4f8e,9a1c,7b8d9e0f1a2b), \
        WPP_DEFINE_BIT(DBG_INIT)   \
        WPP_DEFINE_BIT(DBG_PNP)    \
        WPP_DEFINE_BIT(DBG_IOCTL)  \
        WPP_DEFINE_BIT(DBG_VHF)    \
        WPP_DEFINE_BIT(DBG_ERROR)  \
    )
#endif

#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) \
    WPP_LEVEL_LOGGER(flags)

#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) \
    (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level >= lvl)

#define LOG_INIT(...)        WPP_INIT_TRACING(...)
#define LOG_CLEANUP(...)     WPP_CLEANUP(...)

#define TRACE_INIT(...)      TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, __VA_ARGS__)
#define TRACE_PNP(...)       TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, __VA_ARGS__)
#define TRACE_IOCTL(...)     TraceEvents(TRACE_LEVEL_INFORMATION, DBG_IOCTL, __VA_ARGS__)
#define TRACE_VHF(...)       TraceEvents(TRACE_LEVEL_INFORMATION, DBG_VHF, __VA_ARGS__)
#define TRACE_ERROR(...)     TraceEvents(TRACE_LEVEL_ERROR, DBG_ERROR, __VA_ARGS__)
