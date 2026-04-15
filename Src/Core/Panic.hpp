#pragma once

#include <Core/Defines.hpp>
#include <Core/Log.hpp>
#include <Renderer/Backend/RxUtil.hpp>

namespace fx {

void Terminate();

#ifdef FX_PLATFORM_WINDOWS
#define FX_BREAKPOINT __debugbreak()
#elif FX_PLATFORM_MACOS
#define FX_BREAKPOINT __builtin_debugtrap()
#endif

template <typename... TTypes>
void Panic(const char* module, const char* fmt, TTypes&&... items)
{
    LogFatal("An irrecoverable error has occurred");

    if (module != nullptr) {
        LogFatal("{:s}: ", module);
    }

    LogFatal(fmt, std::forward<TTypes>(items)...);

    Terminate();
}

template <typename... TTypes>
void PanicVulkan(const char* module, const char* fmt, VkResult result, TTypes&&... items)
{
    LogFatal("An irrecoverable error has occurred");

    if (module != nullptr) {
        LogFatal("{:s}: ", module);
    }

    LogFatal(fmt, std::forward<TTypes>(items)...);
    LogFatal("=> Vulkan Err: {:s}", renderer::RxUtil::ResultToStr(result));

    Terminate();
}

#define ModulePanic(...)       Panic(ModuleName__, __VA_ARGS__)
#define ModulePanicVulkan(...) PanicVulkan(ModuleName__, __VA_ARGS__)

#define Assert(cond)                                                                                                   \
    if (!(cond)) {                                                                                                     \
        LogFatal("An assertion failed (Cond: {:s}) at ({:s}:{:d})", #cond, __FILE__, __LINE__);                        \
        Panic(__func__, "Assertion failed!", 0);                                                                       \
    }

#define AssertMsg(cond_, msg_)                                                                                         \
    if (!(cond_)) {                                                                                                    \
        LogFatal("An assertion failed (Cond: {:s}) at ({:s}:{:d})", #cond_, __FILE__, __LINE__);                       \
        LogFatal("Assertion Msg: {:s}", msg_);                                                                         \
        Panic(__func__, "Assertion failed!", 0);                                                                       \
    }

#if defined(FX_BUILD_DEBUG) && !defined(FX_NO_DEBUG_ASSERTS)
#define DebugAssert(cond)                                                                                              \
    {                                                                                                                  \
        if (!(cond)) {                                                                                                 \
            LogFatal("=== DEBUG ASSERTION FAILED ===");                                                                \
            LogFatal("An assertion failed (Cond: {:s}) at ({:s}:{:d})", #cond, __FILE__, __LINE__);                    \
            Panic(__func__, "Debug assertion failed!");                                                                \
        }                                                                                                              \
    }
#else
#define DebugAssert(cond)
#endif

} // namespace fx
