#pragma once

#include <Core/FxDefines.hpp>

#include <Core/Log.hpp>
#include <Renderer/Backend/RxUtil.hpp>

#include <vulkan/vulkan.h>

#define FX_BREAKPOINT __builtin_trap()

template <typename T, typename... Types>
void FxPanic(const char* const module, const char* fmt, T first, Types... items)
{
    Log::LogSeverityText<Log::Severity::Fatal>();

    if (module != nullptr) {
        Log::Write("%s: ", module);
    }

    Log::Write(fmt, items...);
    Log::Write("\n");

    std::terminate();
}

template <typename... Types>
void FxPanic(const char* module, const char* fmt, VkResult result, Types... items)
{
    Log::LogSeverityText<Log::Severity::Fatal>();

    if (module != nullptr) {
        Log::Write("%s: ", module);
    }

    Log::Write(fmt, items...);
    Log::Write("\n");

    printf("=> Vulkan Err: %s\n", RxUtil::ResultToStr(result));

    std::terminate();
}


#define FxModulePanic(...) \
    FxPanic(FxModuleName__, __VA_ARGS__)

#define FxAssert(cond) \
    if (!(cond)) { \
        Log::Fatal("", 0); \
        Log::Fatal("An assertion failed (Cond: %s) at (%s:%d)", #cond, __FILE__, __LINE__); \
        FxPanic(__func__, "Assertion failed!", 0); \
    }

#if defined(FX_BUILD_DEBUG) && !defined(FX_NO_DEBUG_ASSERTS)
#define FxDebugAssert(cond) \
    { \
        if (!(cond)) { \
            Log::Fatal("=== DEBUG ASSERTION FAILED ===", 0); \
            Log::Fatal("An assertion failed (Cond: %s) at (%s:%d)", #cond, __FILE__, __LINE__); \
            FxPanic(__func__, "Debug assertion failed!", 0); \
        } \
    }
#else
#define FxDebugAssert(cond)
#endif
