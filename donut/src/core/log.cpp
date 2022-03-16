
#include <donut/core/log.h>
#include <stdio.h>
#include <stdarg.h>
#include <iterator>
#if _WIN32
#include <Windows.h>
#endif

namespace donut::log
{
    static constexpr size_t g_MessageBufferSize = 4096;
    
    void DefaultCallback(Severity severity, const char* message)
    {
        const char* severityText = "ERROR";
        switch (severity)
        {
        case Severity::Info:
            severityText = "INFO";
            break;
        case Severity::Warning:
            severityText = "WARNING";
            break;
        case Severity::Error:
            severityText = "ERROR";
            break;
        case Severity::Fatal:
            severityText = "FATAL ERROR";
            break;
        }

        char buf[g_MessageBufferSize];
        snprintf(buf, std::size(buf), "%s: %s", severityText, message);

#if _WIN32
        OutputDebugStringA(buf);
        OutputDebugStringA("\n");

        if (severity == Severity::Error || severity == Severity::Fatal)
        {
            MessageBoxA(0, buf, "Error", MB_ICONERROR);
        }
#else
        fprintf(stderr, "%s\n", buf);
#endif

        if (severity == Severity::Fatal)
            abort();
    }

    static Callback g_Callback = &DefaultCallback;
    static Severity g_MinSeverity = Severity::Info;

    void SetMinSeverity(Severity severity)
    {
        g_MinSeverity = severity;
    }

    void SetCallback(Callback func)
    {
        g_Callback = func;
    }

    void ResetCallback()
    {
        g_Callback = &DefaultCallback;
    }

    void message(Severity severity, const char* fmt...)
    {
        if (static_cast<int>(g_MinSeverity) > static_cast<int>(severity))
            return;

        char buffer[g_MessageBufferSize];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, std::size(buffer), fmt, args);

        g_Callback(severity, buffer);

        va_end(args);
    }

    void info(const char* fmt...)
    {
        if (static_cast<int>(g_MinSeverity) > static_cast<int>(Severity::Info))
            return;

        char buffer[g_MessageBufferSize];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, std::size(buffer), fmt, args);

        g_Callback(Severity::Info, buffer);

        va_end(args);
    }

    void warning(const char* fmt...)
    {
        if (static_cast<int>(g_MinSeverity) > static_cast<int>(Severity::Warning))
            return;

        char buffer[g_MessageBufferSize];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, std::size(buffer), fmt, args);

        g_Callback(Severity::Warning, buffer);

        va_end(args);
    }

    void error(const char* fmt...)
    {
        if (static_cast<int>(g_MinSeverity) > static_cast<int>(Severity::Error))
            return;

        char buffer[g_MessageBufferSize];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, std::size(buffer), fmt, args);

        g_Callback(Severity::Error, buffer);

        va_end(args);
    }

    void fatal(const char* fmt...)
    {
        char buffer[g_MessageBufferSize];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, std::size(buffer), fmt, args);

        g_Callback(Severity::Fatal, buffer);

        va_end(args);
    }
}
