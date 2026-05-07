#pragma once

#include "Panic.hpp"
#include "Types.hpp"


namespace fx {


template <typename T, unsigned int TCapacity>
class StackArray
{
public:
    StackArray() = default;

    StackArray(const std::initializer_list<T>& list)
    {
        const size_t list_size = list.size();
        if (list_size > Capacity) {
            Panic("StackArray", "Stack array cannot be resized! {} > {}", list_size, Capacity);
            return;
        }

        for (const T& obj : list) {
            Insert(obj);
        }
    }

    StackArray<T, TCapacity>& operator=(const StackArray<T, TCapacity>& other)
    {
        Clear();

        for (uint32 i = 0; i < other.Size; i++) {
            Insert(other.pData[i]);
        }

        return *this;
    }

    T* Insert(const T& value)
    {
        if (Size >= Capacity) {
            Panic("StackArray", "Insert out of range");
        }
        pData[Size++] = value;
        return &pData[Size - 1];
    }

    T* Insert()
    {
        ++Size;
        return &pData[Size - 1];
    }

    const T& operator[](size_t index) const
    {
        if (index >= Capacity) {
            Panic("StackArray", "Access out of range");
        }
        return pData[index];
    }

    T& operator[](size_t index)
    {
        if (index >= Capacity) {
            Panic("StackArray", "Access out of range");
        }
        return pData[index];
    }

    void MarkFull() { Size = Capacity; };
    void Clear() { Size = 0; }

    uint32 GetSizeInBytes() const { return Size * sizeof(T); }

    T& Last() { return pData[Size]; }

    operator T*() noexcept { return pData; }

    operator const T*() const noexcept { return pData; }

public:
    uint32 Size = 0;
    const uint32 Capacity = TCapacity;

    T pData[TCapacity];
};

} // namespace fx
