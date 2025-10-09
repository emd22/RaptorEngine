#pragma once

#include "FxPanic.hpp"
#include "FxTypes.hpp"

#include <stdexcept>

template <typename T, unsigned int TCapacity>
class FxStackArray
{
public:
    FxStackArray() = default;

    FxStackArray(const std::initializer_list<T>& list)
    {
        const size_t list_size = list.size();
        if (list_size > Capacity) {
            FxPanic("FxStackArray", "Stack array cannot be resized! {} > {}", list_size, Capacity);
            return;
        }

        for (const T& obj : list) {
            Insert(obj);
        }
    }

    T* Insert(const T& value)
    {
        if (Size + 1 > Capacity) {
            throw std::out_of_range("FxStackArray insert out of range");
        }
        Data[Size++] = value;
        return &Data[Size - 1];
    }

    const T& operator[](size_t index) const
    {
        if (index > Capacity) {
            throw std::out_of_range("FxStackArray access out of range");
        }
        return Data[index];
    }

    T& operator[](size_t index)
    {
        if (index > Capacity) {
            throw std::out_of_range("FxStackArray access out of range");
        }
        return Data[index];
    }

    T& Last() { return Data[Size]; }

    operator T*() noexcept { return Data; }

    operator const T*() const noexcept { return Data; }

public:
    uint32 Size = 0;
    const uint32 Capacity = TCapacity;

    T Data[TCapacity];
};
