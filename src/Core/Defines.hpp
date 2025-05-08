#pragma once

#ifdef FX_NO_SIMD
    // FX_NO_SIMD defined
#elif defined(__ARM_NEON__)
    #define FX_USE_NEON 1
#else
    #define FX_NO_SIMD 1
#endif

#ifdef __APPLE__
    #define FX_PLATFORM_MACOS 1
#elif _WIN64
    #define FX_PLATFORM_WINDOWS 1
#else
    #error "Unsupported platform"
#endif

#define FX_SET_MODULE_NAME(str) \
    static const char *FxModuleName__ = str;
