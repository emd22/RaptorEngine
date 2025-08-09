#pragma once

#include <type_traits>
#include <Core/FxDefines.hpp>


#if defined(FX_COMPILER_CLANG) || defined(FX_COMPILER_GCC)
#define FX_UTIL_DEFINE_DEMANGLE_NAME 1
#include <cxxabi.h>
#endif

#define FxSizeofArray(arr_) (sizeof((arr_)) / sizeof((arr_)[0]))

class FxUtil
{
public:
    template <typename EnumClass>
    static std::underlying_type<EnumClass>::type EnumToInt(EnumClass value)
    {
        return static_cast<typename std::underlying_type<EnumClass>::type>(value);
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




// https://en.cppreference.com/w/cpp/types/remove_reference
// template <class T> struct RemoveReference { typedef T type; };
// template <class T> struct RemoveReference<T &> { typedef T type; };
// template <class T> struct RemoveReference<T &&> { typedef T type; };

// template <typename T>
// constexpr RemoveReference<T>::type &&PtrMove(T &&object) noexcept
// {
//     return static_cast<RemoveReference<T>::type &&>(object);
// }
