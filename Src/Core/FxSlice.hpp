#pragma once

#include "Types.hpp"
#include "FxSizedArray.hpp"
#include "FxStackArray.hpp"

template <typename T>
struct FxSlice
{
    T* Ptr = nullptr;
    uint32 Size;

public:
    using Iterator = T*;
    using ConstIterator = T*;

    Iterator begin() const
    {
        return Ptr;
    }

    Iterator end() const
    {
        return Ptr + Size;
    }

    FxSlice(const FxSizedArray<T>& sized_arr)
        : Ptr(sized_arr.Data), Size(sized_arr.Size)
    {
    }

    template <uint32 TSize>
    FxSlice(const FxStackArray<T, TSize>& stack_arr)
        : Ptr(stack_arr.Data), Size(stack_arr.Size)
    {
    }

    FxSlice(const T* ptr, uint32 size)
        : Ptr(ptr), Size(size)
    {
    }

    FxSlice(T* ptr, uint32 size)
        : Ptr(ptr), Size(size)
    {
    }

    FxSlice(FxSlice& other) = delete;
    FxSlice(const FxSlice& other) = delete;
    FxSlice& operator = (T* value) = delete;

    operator T* () const
    {
        return Ptr;
    }

    T& operator [] (size_t index)
    {
        if (index > Size) {
            FxPanic("FxSlice", "Index out of range! (%zu > %u)\n", index, Size);
        }
        return Ptr[index];
    }
};

/** Creates a new FxSlice object */
template <typename T>
FxSlice<T> FxMakeSlice(T* ptr, uint32 size)
{
    return FxSlice<T>(ptr, size);
}
