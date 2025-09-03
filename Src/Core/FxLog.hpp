#pragma once

#include <Core/FxDefines.hpp>
#include <format>
#include <fstream>
#include <iostream>
#include <string>

enum class FxLogChannel
{
    None,

    Debug,
    Info,
    Warning,
    Error,
    Fatal,
};

#define FX_LOG_OUTPUT_TO_STDOUT 1
#define FX_LOG_OUTPUT_TO_FILE   1


// XXX: Remove this old macro
#ifdef FX_DISABLE_LOG_COLOR

#ifdef FX_LOG_ENABLE_COLORS
#undef FX_LOG_ENABLE_COLORS
#endif

#else

#ifndef FX_LOG_ENABLE_COLORS
#define FX_LOG_ENABLE_COLORS 1
#endif

#endif

#define FX_LOG_CHANNEL_LABEL_DEBUG "[DEBUG]"
#define FX_LOG_CHANNEL_LABEL_INFO  "[INFO] "
#define FX_LOG_CHANNEL_LABEL_WARN  "[WARN] "
#define FX_LOG_CHANNEL_LABEL_ERROR "[ERROR]"
#define FX_LOG_CHANNEL_LABEL_FATAL "[FATAL]"

#define FX_LOG_STYLE_RESET "\x1b[0m "

std::ofstream& FxLogGetFile(bool* can_write);
void FxLogCreateFile(const std::string& path);

template <FxLogChannel TLogChannel, bool TAllowColors = true>
constexpr std::string FxLogChannelText()
{
    if constexpr (TLogChannel == FxLogChannel::None) {
        return "";
    }

#ifdef FX_LOG_ENABLE_COLORS
    constexpr bool AllowColors = TAllowColors;
#else
    // Colours are disabled globally on compilation, never show them
    constexpr bool AllowColors = false;
#endif

    if (AllowColors) {
        if constexpr (TLogChannel == FxLogChannel::Debug) {
            return ("\x1b[92m" FX_LOG_CHANNEL_LABEL_DEBUG FX_LOG_STYLE_RESET);
        }
        else if constexpr (TLogChannel == FxLogChannel::Info) {
            return ("\x1b[94m" FX_LOG_CHANNEL_LABEL_INFO FX_LOG_STYLE_RESET);
        }
        else if constexpr (TLogChannel == FxLogChannel::Warning) {
            return ("\x1b[94m" FX_LOG_CHANNEL_LABEL_WARN FX_LOG_STYLE_RESET);
        }
        else if constexpr (TLogChannel == FxLogChannel::Error) {
            return ("\x1b[91m" FX_LOG_CHANNEL_LABEL_ERROR FX_LOG_STYLE_RESET);
        }
        else if constexpr (TLogChannel == FxLogChannel::Fatal) {
            return ("\x1b[1;91m" FX_LOG_CHANNEL_LABEL_FATAL FX_LOG_STYLE_RESET);
        }
    }
    else {
        if constexpr (TLogChannel == FxLogChannel::Debug) {
            return FX_LOG_CHANNEL_LABEL_DEBUG;
        }
        else if constexpr (TLogChannel == FxLogChannel::Info) {
            return FX_LOG_CHANNEL_LABEL_INFO;
        }
        else if constexpr (TLogChannel == FxLogChannel::Warning) {
            return FX_LOG_CHANNEL_LABEL_WARN;
        }
        else if constexpr (TLogChannel == FxLogChannel::Error) {
            return FX_LOG_CHANNEL_LABEL_ERROR;
        }
        else if constexpr (TLogChannel == FxLogChannel::Fatal) {
            return FX_LOG_CHANNEL_LABEL_FATAL;
        }
    }

    return "";
}


template <FxLogChannel TChannel, typename... TTypes>
void FxLogToStdout(std::string_view fmt, TTypes&&... args)
{
    // Disregard debug logs when building for release
#ifdef FX_BUILD_RELEASE
    if constexpr (TChannel == FxLogChannel::Debug) {
        return;
    }
#endif

    auto msg = std::vformat(fmt, std::make_format_args(args...));
    auto channel = FxLogChannelText<TChannel>();

    std::cout << channel << msg << '\n';
}

template <FxLogChannel TChannel, typename... TTypes>
void FxLogToFile(std::string_view fmt, TTypes&&... args)
{
    // Disregard debug logs when building for release
#ifdef FX_BUILD_RELEASE
    if constexpr (TChannel == FxLogChannel::Debug) {
        return;
    }
#endif

    bool can_write = false;
    std::ofstream& stream = FxLogGetFile(&can_write);

    if (!can_write) {
        return;
    }

    auto msg = std::vformat(fmt, std::make_format_args(args...));
    auto channel = FxLogChannelText<TChannel, false>();

    stream << channel << msg << '\n';
}

template <FxLogChannel TChannel, typename... TTypes>
void FxLog(std::string_view fmt, TTypes&&... args)
{
    // Disregard debug logs when building for release
#ifdef FX_BUILD_RELEASE
    if constexpr (TChannel == FxLogChannel::Debug) {
        return;
    }
#endif

#ifdef FX_LOG_OUTPUT_TO_STDOUT
    FxLogToStdout<TChannel>(fmt, std::forward<TTypes>(args)...);
#endif

#ifdef FX_LOG_OUTPUT_TO_FILE
    FxLogToFile<TChannel>(fmt, std::forward<TTypes>(args)...);
#endif
}


template <typename... TTypes>
void FxLog(std::string_view fmt, TTypes&&... args)
{
    FxLog<FxLogChannel::Debug>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void FxLogError(std::string_view fmt, TTypes&&... args)
{
    FxLog<FxLogChannel::Error>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void FxLogFatal(std::string_view fmt, TTypes&&... args)
{
    FxLog<FxLogChannel::Fatal>(fmt, std::forward<TTypes>(args)...);
}
