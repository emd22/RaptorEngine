#pragma once

#include <type_traits>

template <typename EnumClass>
std::underlying_type<EnumClass>::type EnumToInt(EnumClass value)
{
    return static_cast<typename std::underlying_type<EnumClass>::type>(value);
}

// https://en.cppreference.com/w/cpp/types/remove_reference
template <class T> struct RemoveReference { typedef T type; };
template <class T> struct RemoveReference<T &> { typedef T type; };
template <class T> struct RemoveReference<T &&> { typedef T type; };

template <typename T>
constexpr RemoveReference<T>::type &&PtrMove(T &&object) noexcept
{
    return static_cast<RemoveReference<T>::type &&>(object);
}
