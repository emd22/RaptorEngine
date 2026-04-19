#pragma once

#include "Defines.hpp"

#include <format>
#include <fstream>
#include <iostream>
#include <string>

namespace fx {

enum class eLogChannel
{
    None,
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
};


#define FX_LOG_CHANNEL_LABEL_DEBUG "[DEBUG] "
#define FX_LOG_CHANNEL_LABEL_INFO  "[INFO]  "
#define FX_LOG_CHANNEL_LABEL_WARN  "[WARN]  "
#define FX_LOG_CHANNEL_LABEL_ERROR "[ERROR] "
#define FX_LOG_CHANNEL_LABEL_FATAL "[FATAL] "

#define FX_LOG_STYLE_RESET "\x1b[0m"

std::ofstream& LogGetFile(bool* can_write);
void LogCreateFile(const std::string& path);

template <eLogChannel TeLogChannel, bool TAllowColors = true>
constexpr std::string LogGetChannelText()
{
    if constexpr (TeLogChannel == eLogChannel::None) {
        return "";
    }

#ifdef FX_LOG_ENABLE_COLORS
    constexpr bool AllowColors = TAllowColors;
#else
    // Colours are disabled globally on compilation, never show them
    constexpr bool AllowColors = false;
#endif

    if (AllowColors) {
        if constexpr (TeLogChannel == eLogChannel::Debug) {
            return ("\x1b[92m" FX_LOG_CHANNEL_LABEL_DEBUG FX_LOG_STYLE_RESET);
        }
        else if constexpr (TeLogChannel == eLogChannel::Info) {
            return ("\x1b[94m" FX_LOG_CHANNEL_LABEL_INFO FX_LOG_STYLE_RESET);
        }
        else if constexpr (TeLogChannel == eLogChannel::Warning) {
            return ("\x1b[93m" FX_LOG_CHANNEL_LABEL_WARN FX_LOG_STYLE_RESET);
        }
        else if constexpr (TeLogChannel == eLogChannel::Error) {
            return ("\x1b[91m" FX_LOG_CHANNEL_LABEL_ERROR FX_LOG_STYLE_RESET);
        }
        else if constexpr (TeLogChannel == eLogChannel::Fatal) {
            return ("\x1b[1;91m" FX_LOG_CHANNEL_LABEL_FATAL FX_LOG_STYLE_RESET);
        }
    }
    else {
        if constexpr (TeLogChannel == eLogChannel::Debug) {
            return FX_LOG_CHANNEL_LABEL_DEBUG;
        }
        else if constexpr (TeLogChannel == eLogChannel::Info) {
            return FX_LOG_CHANNEL_LABEL_INFO;
        }
        else if constexpr (TeLogChannel == eLogChannel::Warning) {
            return FX_LOG_CHANNEL_LABEL_WARN;
        }
        else if constexpr (TeLogChannel == eLogChannel::Error) {
            return FX_LOG_CHANNEL_LABEL_ERROR;
        }
        else if constexpr (TeLogChannel == eLogChannel::Fatal) {
            return FX_LOG_CHANNEL_LABEL_FATAL;
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

template <eLogChannel TChannel, typename... TTypes>
void LogToStdout(std::string_view fmt, TTypes&&... args)
{
    // Disregard debug logs when building for release
#ifdef FX_BUILD_RELEASE
    if constexpr (TChannel == eLogChannel::Debug) {
        return;
    }
#endif

    auto msg = std::vformat(fmt, std::make_format_args(args...));

    // If the channel is set to `None`, do not print the channel name
    if constexpr (TChannel == eLogChannel::None) {
        std::cout << msg << '\n';
        return;
    }

    auto channel = LogGetChannelText<TChannel>();

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


template <eLogChannel TChannel, typename... TTypes>
void LogToFile(std::string_view fmt, TTypes&&... args)
{
    // Disregard debug logs when building for release
#ifdef FX_BUILD_RELEASE
    if constexpr (TChannel == eLogChannel::Debug) {
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
    if constexpr (TChannel == eLogChannel::None) {
        stream << msg << '\n';
        return;
    }

    auto channel = LogGetChannelText<TChannel, false>();

    stream << channel << msg << '\n';
}

/**
 * @brief Logs a message to a channel from `eLogChannel`.
 */
template <eLogChannel TChannel, typename... TTypes>
void Log(std::string_view fmt, TTypes&&... args)
{
    // Disregard debug logs when building for release
#ifdef FX_BUILD_RELEASE
    if constexpr (TChannel == eLogChannel::Debug) {
        return;
    }
#endif

#ifdef FX_LOG_OUTPUT_TO_STDOUT
    LogToStdout<TChannel>(fmt, std::forward<TTypes>(args)...);
#endif

#ifdef FX_LOG_OUTPUT_TO_FILE
    LogToFile<TChannel>(fmt, std::forward<TTypes>(args)...);
#endif
}

/**
 * @brief Logs a message to a channel from `eLogChannel`.
 */
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

template <typename... TTypes>
void LogInfo(std::string_view fmt, TTypes&&... args)
{
    Log<eLogChannel::Info>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void LogDebug(std::string_view fmt, TTypes&&... args)
{
    Log<eLogChannel::Debug>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void LogWarning(std::string_view fmt, TTypes&&... args)
{
    Log<eLogChannel::Warning>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void LogError(std::string_view fmt, TTypes&&... args)
{
    Log<eLogChannel::Error>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void LogFatal(std::string_view fmt, TTypes&&... args)
{
    Log<eLogChannel::Fatal>(fmt, std::forward<TTypes>(args)...);
}


template <eLogChannel TChannel>
constexpr void eLogChannelText()
{
#ifdef FX_LOG_OUTPUT_TO_STDOUT
    LogDirectToStdout("{}", LogGetChannelText<TChannel>());
#endif
#ifdef FX_LOG_OUTPUT_TO_FILE
    LogDirectToFile("{}", LogGetChannelText<TChannel, false>());
#endif
}

} // namespace fx
