#pragma once

#include "SizedArray.hpp"
#include "StackArray.hpp"
#include "Types.hpp"

namespace fx {

template <typename TDataType>
struct Slice
{
    TDataType* pData = nullptr;
    uint32 Size;

public:
    using Iterator = TDataType*;
    using ConstIterator = TDataType*;

    Iterator begin() const { return pData; }

    Iterator end() const { return pData + Size; }

    Slice(const SizedArray<TDataType>& sized_arr) : pData(sized_arr.pData), Size(sized_arr.Size) {}

    template <uint32 TSize>
    Slice(StackArray<TDataType, TSize>& stack_arr) : pData(stack_arr.pData), Size(stack_arr.Size)
    {
    }

    Slice(TDataType* ptr, uint32 size) : pData(ptr), Size(size) {}

    Slice(nullptr_t np) : pData(nullptr), Size(0) {}

    Slice(const Slice& other)
    {
        pData = other.pData;
        Size = other.Size;
    }

    template <typename TOtherType>
    Slice(const Slice<TOtherType>& other)
    {
        pData = reinterpret_cast<TDataType*>(other.pData);
        Size = other.Size;
    }

    Slice& operator=(TDataType* value) = delete;

    bool operator==(nullptr_t np) { return pData == nullptr; }

    operator TDataType*() const { return pData; }

    TDataType& operator[](size_t index)
    {
        if (index > Size) {
            Panic("Slice", "Index out of range! ({:d} > {:d})\n", index, Size);
        }
        return pData[index];
    }
};

/** Creates a new Slice object */
template <typename T>
Slice<T> MakeSlice(T* ptr, uint32 size)
{
    return Slice<T>(ptr, size);
}

} // namespace fx
