#pragma once

#include <Core/Defines.hpp>

#include <Core/Log.hpp>
#include <Renderer/Backend/RvkUtil.hpp>

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

    printf("=> Vulkan Err: %s\n", RvkUtil::ResultToStr(result));

    std::terminate();
}


#define FxModulePanic(...) \
    FxPanic(FxModuleName__, __VA_ARGS__)

#define FxAssert(cond) \
    if (!(cond)) { \
        printf("Assertion failed! (Cond: %s)\n", #cond); \
        FxPanic(__func__, "Assertion failed!", 0); \
    }
