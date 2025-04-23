#pragma once

#ifdef FX_NO_SIMD
    // FX_NO_SIMD defined
#elif defined(__ARM_NEON__)
    #define FX_USE_NEON 1
#else
    #define FX_NO_SIMD 1
#endif

#define FX_SET_MODULE_NAME(str) \
    static const char *FxModuleName__ = str;
