#pragma once

namespace donut::log
{
    enum class Severity
    {
        Info = 0,
        Warning,
        Error,
        Fatal
    };

    typedef void(*Callback)(Severity, const char*);

    void SetMinSeverity(Severity severity);
    void SetCallback(Callback func);
    void ResetCallback();

    void message(Severity severity, const char* fmt...);
    void info(const char* fmt...);
    void warning(const char* fmt...);
    void error(const char* fmt...);
    void fatal(const char* fmt...);
}
