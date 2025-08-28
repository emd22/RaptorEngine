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


#ifdef __clang__
#define FX_COMPILER_CLANG 1
#elif __GNUC__
#define FX_COMPILER_GCC 1
#elif _MSC_VER
#define FX_COMPILER_MSVC 1
#else
#error "Unsupported compiler"
#endif

#ifdef NDEBUG
#define FX_BUILD_RELEASE
#else
#define FX_BUILD_DEBUG
#endif

#define FX_SET_MODULE_NAME(str) [[maybe_unused]] static const char* FxModuleName__ = str;

#ifdef _WIN64
#define FX_FORCE_INLINE __forceinline
#else
#define FX_FORCE_INLINE __attribute__((always_inline))
#endif
