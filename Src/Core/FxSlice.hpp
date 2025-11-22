#pragma once

#include "FxSizedArray.hpp"
#include "FxStackArray.hpp"
#include "FxTypes.hpp"

template <typename T>
struct FxSlice
{
    T* pData = nullptr;
    uint32 Size;

public:
    using Iterator = T*;
    using ConstIterator = T*;

    Iterator begin() const { return pData; }

    Iterator end() const { return pData + Size; }

    FxSlice(const FxSizedArray<T>& sized_arr) : pData(sized_arr.pData), Size(sized_arr.Size) {}

    template <uint32 TSize>
    FxSlice(FxStackArray<T, TSize>& stack_arr) : pData(stack_arr.pData), Size(stack_arr.Size)
    {
    }

    FxSlice(T* ptr, uint32 size) : pData(ptr), Size(size) {}

    FxSlice(nullptr_t np) : pData(nullptr), Size(0) {}

    FxSlice(const FxSlice& other)
    {
        pData = other.pData;
        Size = other.Size;
    }

    template <typename TOtherType>
    FxSlice(const FxSlice<TOtherType>& other)
    {
        pData = reinterpret_cast<T*>(other.pData);
        Size = other.Size;
    }

    FxSlice& operator=(T* value) = delete;

    bool operator==(nullptr_t np) { return pData == nullptr; }

    operator T*() const { return pData; }

    T& operator[](size_t index)
    {
        if (index > Size) {
            FxPanic("FxSlice", "Index out of range! ({:d} > {:d})\n", index, Size);
        }
        return pData[index];
    }
};

/** Creates a new FxSlice object */
template <typename T>
FxSlice<T> FxMakeSlice(T* ptr, uint32 size)
{
    return FxSlice<T>(ptr, size);
}
