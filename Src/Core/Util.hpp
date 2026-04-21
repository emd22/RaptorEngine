#pragma once

#include <stdio.h>

#include <Core/Types.hpp>
#include <cstdio>
#include <type_traits>

#define SizeofArray(arr_) std::size(arr)

class Util
{
public:
    template <typename TEnumClass>
    static constexpr std::underlying_type<TEnumClass>::type EnumToInt(TEnumClass value)
    {
        return static_cast<typename std::underlying_type<TEnumClass>::type>(value);
    }

    template <typename TEnumClass, typename TOutputType>
    static constexpr TOutputType EnumToInt(TEnumClass value)
    {
        return static_cast<TOutputType>(value);
    }

    static FILE* FileOpen(const char* path, const char* mode)
    {
        // TODO: readd fopen_s for Windows;
        return fopen(path, mode);
    }

    static int DemangleName(const char* mangled_name, char* buffer, size_t buffer_size);
};

/*
template <typename TEnum>
struct EnumFlags
{
    using FlagsType = std::underlying_type_t<TEnum>;

public:
    EnumFlags() = default;

    ////////////////////////
    // Unary Operators
    ////////////////////////

    constexpr EnumFlags operator ~ ()
    {
        return ~(Flags);
    }

    ////////////////////////
    // Binary Operators
    ////////////////////////

    constexpr EnumFlags operator | (EnumFlags other) const
    {
        return (Flags | other.Flags);
    }

    constexpr EnumFlags operator & (EnumFlags other) const
    {
        return (Flags & other.Flags);
    }

    EnumFlags& operator |= (EnumFlags other)
    {
        Flags |= other.Flags;
        return *this;
    }

    EnumFlags& operator &= (EnumFlags other)
    {
        Flags &= other.Flags;
        return *this;
    }

public:
    FlagsType Flags = 0;
};

*/
