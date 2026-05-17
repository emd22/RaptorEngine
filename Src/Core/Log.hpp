#pragma once

#include "Defines.hpp"

#include <format>
#include <fstream>
#include <iostream>
#include <string>

namespace fx {

enum class eLogSeverity
{
    None,
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
};

enum eLogCategory
{
    LC_CORE,
    LC_SHADER,
    LC_RENDER,
    LC_PHYSICS,
    LC_MEMORY,
    LC_ASSET,
    LC_SCRIPT,
};


#define FX_LOG_SEVERITY_LABEL_DEBUG "[DEBUG] "
#define FX_LOG_SEVERITY_LABEL_INFO  "[INFO]  "
#define FX_LOG_SEVERITY_LABEL_WARN  "[WARN]  "
#define FX_LOG_SEVERITY_LABEL_ERROR "[ERROR] "
#define FX_LOG_SEVERITY_LABEL_FATAL "[FATAL] "

// High intensity colours
#define FX_LOG_COLOR_HI_RED      "\x1b[91m"
#define FX_LOG_COLOR_HI_RED_BOLD "\x1b[1;91m"
#define FX_LOG_COLOR_HI_GREEN    "\x1b[92m"
#define FX_LOG_COLOR_HI_YELLOW   "\x1b[93m"
#define FX_LOG_COLOR_HI_BLUE     "\x1b[94m"

#define FX_LOG_COLOR_FG(color_code_) "\x1b[38;5;" #color_code_ "m"

#define FX_LOG_STYLE_RESET "\x1b[0m"

std::ofstream& LogGetFile(bool* can_write);
void LogCreateFile(const std::string& path);

void LogCategoryText(eLogCategory category);

template <eLogSeverity TSeverity, bool TAllowColors = true>
constexpr std::string LogGetSeverityText()
{
    if constexpr (TSeverity == eLogSeverity::None) {
        return "";
    }

#ifdef FX_LOG_ENABLE_COLORS
    constexpr bool AllowColors = TAllowColors;
#else
    // Colours are disabled globally on compilation, never show them
    constexpr bool AllowColors = false;
#endif

    if (AllowColors) {
        if constexpr (TSeverity == eLogSeverity::Debug) {
            return (FX_LOG_COLOR_HI_GREEN FX_LOG_SEVERITY_LABEL_DEBUG FX_LOG_STYLE_RESET);
        }
        else if constexpr (TSeverity == eLogSeverity::Info) {
            return (FX_LOG_COLOR_HI_BLUE FX_LOG_SEVERITY_LABEL_INFO FX_LOG_STYLE_RESET);
        }
        else if constexpr (TSeverity == eLogSeverity::Warning) {
            return (FX_LOG_COLOR_HI_YELLOW FX_LOG_SEVERITY_LABEL_WARN FX_LOG_STYLE_RESET);
        }
        else if constexpr (TSeverity == eLogSeverity::Error) {
            return (FX_LOG_COLOR_HI_RED FX_LOG_SEVERITY_LABEL_ERROR FX_LOG_STYLE_RESET);
        }
        else if constexpr (TSeverity == eLogSeverity::Fatal) {
            return (FX_LOG_COLOR_HI_RED_BOLD FX_LOG_SEVERITY_LABEL_FATAL FX_LOG_STYLE_RESET);
        }
    }
    else {
        if constexpr (TSeverity == eLogSeverity::Debug) {
            return FX_LOG_SEVERITY_LABEL_DEBUG;
        }
        else if constexpr (TSeverity == eLogSeverity::Info) {
            return FX_LOG_SEVERITY_LABEL_INFO;
        }
        else if constexpr (TSeverity == eLogSeverity::Warning) {
            return FX_LOG_SEVERITY_LABEL_WARN;
        }
        else if constexpr (TSeverity == eLogSeverity::Error) {
            return FX_LOG_SEVERITY_LABEL_ERROR;
        }
        else if constexpr (TSeverity == eLogSeverity::Fatal) {
            return FX_LOG_SEVERITY_LABEL_FATAL;
        }
    }

    return "";
}


template <typename... TTypes>
void LogDirectToStdout(std::string_view fmt, TTypes&&... args)
{
    auto msg = std::vformat(fmt, std::make_format_args(args...));
    std::cout << msg;
}

template <eLogSeverity TChannel, typename... TTypes>
void LogToStdout(std::string_view fmt, TTypes&&... args)
{
    // Disregard debug logs when building for release
#ifdef FX_BUILD_RELEASE
    if constexpr (TChannel == eLogSeverity::Debug) {
        return;
    }
#endif

    auto msg = std::vformat(fmt, std::make_format_args(args...));

    // If the channel is set to `None`, do not print the channel name
    if constexpr (TChannel == eLogSeverity::None) {
        std::cout << msg << '\n';
        return;
    }

    auto channel = LogGetSeverityText<TChannel>();

    std::cout << channel << msg << '\n';
}


template <typename... TTypes>
void LogDirectToFile(std::string_view fmt, TTypes&&... args)
{
    bool can_write = false;
    std::ofstream& stream = LogGetFile(&can_write);

    if (!can_write) {
        return;
    }

    auto msg = std::vformat(fmt, std::make_format_args(args...));

    // If the channel is set to `None`, do not print the channel name
    stream << msg << '\n';
}


template <eLogSeverity TChannel, typename... TTypes>
void LogToFile(std::string_view fmt, TTypes&&... args)
{
    // Disregard debug logs when building for release
#ifdef FX_BUILD_RELEASE
    if constexpr (TChannel == eLogSeverity::Debug) {
        return;
    }
#endif

    bool can_write = false;
    std::ofstream& stream = LogGetFile(&can_write);

    if (!can_write) {
        return;
    }

    auto msg = std::vformat(fmt, std::make_format_args(args...));

    // If the channel is set to `None`, do not print the channel name
    if constexpr (TChannel == eLogSeverity::None) {
        stream << msg << '\n';
        return;
    }

    auto channel = LogGetSeverityText<TChannel, false>();

    stream << channel << msg << '\n';
}

/**
 * @brief Logs a message to a channel from `TChannel`.
 */
template <eLogSeverity TSeverity, typename... TTypes>
void Log(std::string_view fmt, TTypes&&... args)
{
    // Disregard debug logs when building for release
#ifdef FX_BUILD_RELEASE
    if constexpr (TSeverity == eLogSeverity::Debug) {
        return;
    }
#endif

#ifdef FX_LOG_OUTPUT_TO_STDOUT
    LogToStdout<TSeverity>(fmt, std::forward<TTypes>(args)...);
#endif

#ifdef FX_LOG_OUTPUT_TO_FILE
    LogToFile<TSeverity>(fmt, std::forward<TTypes>(args)...);
#endif
}

template <typename... TTypes>
void LogDirect(std::string_view fmt, TTypes&&... args)
{
#ifdef FX_LOG_OUTPUT_TO_STDOUT
    LogDirectToStdout(fmt, std::forward<TTypes>(args)...);
#endif

#ifdef FX_LOG_OUTPUT_TO_FILE
    LogDirectToFile(fmt, std::forward<TTypes>(args)...);
#endif
}

/////////////////////////////////////
// Specialized Log functions
/////////////////////////////////////

template <typename... TTypes>
void LogInfo(std::string_view fmt, TTypes&&... args)
{
    Log<eLogSeverity::Info>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void LogInfo(eLogCategory category, std::string_view fmt, TTypes&&... args)
{
    LogCategoryText(category);
    Log<eLogSeverity::Info>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void LogDebug(std::string_view fmt, TTypes&&... args)
{
    Log<eLogSeverity::Debug>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void LogDebug(eLogCategory category, std::string_view fmt, TTypes&&... args)
{
    LogCategoryText(category);
    Log<eLogSeverity::Debug>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void LogWarning(std::string_view fmt, TTypes&&... args)
{
    Log<eLogSeverity::Warning>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void LogWarning(eLogCategory category, std::string_view fmt, TTypes&&... args)
{
    LogCategoryText(category);
    Log<eLogSeverity::Warning>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void LogError(std::string_view fmt, TTypes&&... args)
{
    Log<eLogSeverity::Error>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void LogError(eLogCategory category, std::string_view fmt, TTypes&&... args)
{
    LogCategoryText(category);
    Log<eLogSeverity::Error>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void LogFatal(std::string_view fmt, TTypes&&... args)
{
    Log<eLogSeverity::Fatal>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void LogFatal(eLogCategory category, std::string_view fmt, TTypes&&... args)
{
    LogCategoryText(category);
    Log<eLogSeverity::Fatal>(fmt, std::forward<TTypes>(args)...);
}


template <eLogSeverity TChannel>
constexpr void LogSeverityText()
{
#ifdef FX_LOG_OUTPUT_TO_STDOUT
    LogDirectToStdout("{}", LogGetSeverityText<TChannel>());
#endif
#ifdef FX_LOG_OUTPUT_TO_FILE
    LogDirectToFile("{}", LogGetChannelText<TChannel, false>());
#endif
}


} // namespace fx
