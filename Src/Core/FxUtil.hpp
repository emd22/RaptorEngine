#pragma once

#include <stdio.h>

#include <Core/FxDefines.hpp>
#include <Core/FxTypes.hpp>
#include <type_traits>

#if defined(FX_COMPILER_CLANG) || defined(FX_COMPILER_GCC)
#define FX_UTIL_DEFINE_DEMANGLE_NAME 1
#include <cxxabi.h>
#endif

#define FxSizeofArray(arr_) (sizeof((arr_)) / sizeof((arr_)[0]))

class FxUtil
{
public:
    template <typename EnumClass>
    static constexpr std::underlying_type<EnumClass>::type EnumToInt(EnumClass value)
    {
        return static_cast<typename std::underlying_type<EnumClass>::type>(value);
    }

    static FILE* FileOpen(const char* path, const char* mode)
    {
        // TODO: readd fopen_s for Windows;
        return fopen(path, mode);
    }

    static int DemangleName(const char* mangled_name, char* buffer, size_t buffer_size)
    {
#ifdef FX_UTIL_DEFINE_DEMANGLE_NAME
        int status;
        abi::__cxa_demangle(mangled_name, buffer, &buffer_size, &status);

        return status;
#else
        // Should we print a warning about this?
        strcpy(mangled_name, buffer);
        return 0;
#endif
    }
};

/*
template <typename TEnum>
struct FxEnumFlags
{
    using FlagsType = std::underlying_type_t<TEnum>;

public:
    FxEnumFlags() = default;

    ////////////////////////
    // Unary Operators
    ////////////////////////

    constexpr FxEnumFlags operator ~ ()
    {
        return ~(Flags);
    }

    ////////////////////////
    // Binary Operators
    ////////////////////////

    constexpr FxEnumFlags operator | (FxEnumFlags other) const
    {
        return (Flags | other.Flags);
    }

    constexpr FxEnumFlags operator & (FxEnumFlags other) const
    {
        return (Flags & other.Flags);
    }

    FxEnumFlags& operator |= (FxEnumFlags other)
    {
        Flags |= other.Flags;
        return *this;
    }

    FxEnumFlags& operator &= (FxEnumFlags other)
    {
        Flags &= other.Flags;
        return *this;
    }

public:
    FlagsType Flags = 0;
};

*/


/**
 * @brief Defines an enum class as a set of bitflags.
 */
#define FX_DEFINE_AS_FLAG_ENUM FlagMax

template <typename T>
decltype(T::FlagMax) operator|(T lhs, T rhs)
{
    return static_cast<T>(static_cast<unsigned int>(lhs) | static_cast<unsigned int>(rhs));
}

template <typename T>
unsigned int operator&(T lhs, T rhs)
{
    return static_cast<unsigned int>(lhs) & static_cast<unsigned int>(rhs);
}

uint32 FxColorFromRGBA(uint8 r, uint8 g, uint8 b, uint8 a);
uint32 FxColorFromFloats(float32 rgba[4]);

// https://en.cppreference.com/w/cpp/types/remove_reference
// template <class T> struct RemoveReference { typedef T type; };
// template <class T> struct RemoveReference<T &> { typedef T type; };
// template <class T> struct RemoveReference<T &&> { typedef T type; };

// template <typename T>
// constexpr RemoveReference<T>::type &&PtrMove(T &&object) noexcept
// {
//     return static_cast<RemoveReference<T>::type &&>(object);
// }
