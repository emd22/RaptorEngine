#pragma once

#include <cstdio>
#include <tuple>

enum class TextColor
{
    Reset = 0,
    Red = 91,
    Green,
    Yellow,
    Blue,
};


/**
 * @deprecated Log using the old logging system. This does not work with log files. Use `FxLog` functions instead.
 */
class OldLog
{
public:
    enum class Severity
    {
        Fatal,
        Error,
        Debug,
        Warning,
        Info,
    };

    // name to print, the text color, is bold text
    using SeverityInfo = std::tuple<const char*, TextColor, bool>;

    template <Severity Level>
    static SeverityInfo GetSeverityInfo()
    {
        if constexpr (Level == Severity::Error) {
            return SeverityInfo { "Error", TextColor::Red, false };
        }
        else if constexpr (Level == Severity::Warning) {
            return SeverityInfo { "Warn", TextColor::Yellow, false };
        }
        else if constexpr (Level == Severity::Debug) {
            return SeverityInfo { "Debug", TextColor::Green, false };
        }
        else if constexpr (Level == Severity::Info) {
            return SeverityInfo { "Info", TextColor::Blue, false };
        }
        else if constexpr (Level == Severity::Fatal) {
            return SeverityInfo { "Fatal", TextColor::Red, true };
        }

        return SeverityInfo { "Unknown", TextColor::Red, false };
    }

    template <typename... Types>
    static void Write(const char* fmt, Types... items)
    {
        // TODO: add file logs
        if constexpr (sizeof...(Types) == 0) {
            printf("%s", fmt);
        }
        else {
            printf(fmt, items...);
        }
    }


    static const char* YesNo(bool value) { return (value) ? "Yes" : "No"; }


    template <OldLog::Severity Severity>
    static void LogSeverityText()
    {
        const auto severity_info = GetSeverityInfo<Severity>();

#ifndef FX_DISABLE_LOG_COLOR
        // print error/warning/info prefix with color
        printf("\x1b[%s%dm[%s]\x1b[0m ",
               std::get<2>(severity_info) ? "1;" : "", // is bold?
               std::get<1>(severity_info),             // color number
               std::get<0>(severity_info)              // log label
        );
#else
        printf("[%s] ", std::get<0>(level_info));
#endif
    }

    template <OldLog::Severity Severity, typename... Types>
    static void LogWithSeverity(const char* fmt, Types... items)
    {
        LogSeverityText<Severity>();

        if constexpr (sizeof...(Types) == 0) {
            puts(fmt);
        }
        else {
            Write(fmt, items...);
            putchar('\n');
        }
    }

    template <typename... Types>
    static void Fatal(const char* fmt, Types... items)
    {
        LogWithSeverity<Severity::Fatal>(fmt, items...);
    }

    template <typename... Types>
    static void Error(const char* fmt, Types... items)
    {
        LogWithSeverity<Severity::Error>(fmt, items...);
    }

    template <typename... Types>
    static void Warning(const char* fmt, Types... items)
    {
        LogWithSeverity<Severity::Warning>(fmt, items...);
    }

    template <typename... Types>
    static void Info(const char* fmt, Types... items)
    {
        LogWithSeverity<Severity::Info>(fmt, items...);
    }

    template <typename... Types>
    static void Debug(const char* fmt, Types... items)
    {
        LogWithSeverity<Severity::Debug>(fmt, items...);
    }
};
