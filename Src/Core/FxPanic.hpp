#pragma once

#include <vulkan/vulkan.h>

#include <Core/FxDefines.hpp>
#include <Core/Log.hpp>
#include <Renderer/Backend/RxUtil.hpp>

#define FX_BREAKPOINT __builtin_trap()

template <typename T, typename... Types>
void FxPanic(const char* const module, const char* fmt, T first, Types... items)
{
    OldLog::LogSeverityText<OldLog::Severity::Fatal>();

    if (module != nullptr) {
        OldLog::Write("%s: ", module);
    }

    OldLog::Write(fmt, items...);
    OldLog::Write("\n");

    std::terminate();
}

template <typename... Types>
void FxPanic(const char* module, const char* fmt, VkResult result, Types... items)
{
    OldLog::LogSeverityText<OldLog::Severity::Fatal>();

    if (module != nullptr) {
        OldLog::Write("%s: ", module);
    }

    OldLog::Write(fmt, items...);
    OldLog::Write("\n");

    printf("=> Vulkan Err: %s\n", RxUtil::ResultToStr(result));

    std::terminate();
}


#define FxModulePanic(...) FxPanic(FxModuleName__, __VA_ARGS__)

#define FxAssert(cond)                                                                                                 \
    if (!(cond)) {                                                                                                     \
        OldLog::Fatal("", 0);                                                                                          \
        OldLog::Fatal("An assertion failed (Cond: %s) at (%s:%d)", #cond, __FILE__, __LINE__);                         \
        FxPanic(__func__, "Assertion failed!", 0);                                                                     \
    }

#if defined(FX_BUILD_DEBUG) && !defined(FX_NO_DEBUG_ASSERTS)
#define FxDebugAssert(cond)                                                                                            \
    {                                                                                                                  \
        if (!(cond)) {                                                                                                 \
            OldLog::Fatal("=== DEBUG ASSERTION FAILED ===", 0);                                                        \
            OldLog::Fatal("An assertion failed (Cond: %s) at (%s:%d)", #cond, __FILE__, __LINE__);                     \
            FxPanic(__func__, "Debug assertion failed!", 0);                                                           \
        }                                                                                                              \
    }
#else
#define FxDebugAssert(cond)
#endif
