#pragma once

#include "SizedArray.hpp"
#include "StackArray.hpp"
#include "Types.hpp"

namespace fx {

template <typename T>
struct Slice
{
    T* pData = nullptr;
    uint32 Size;

public:
    using Iterator = T*;
    using ConstIterator = T*;

    Iterator begin() const { return pData; }

    Iterator end() const { return pData + Size; }

    Slice(const SizedArray<T>& sized_arr) : pData(sized_arr.pData), Size(sized_arr.Size) {}
    Slice(const Slice<T>& other) { (*this) = other; }

    template <typename TOther>
    static Slice<T> FromArray(const SizedArray<TOther>& arr)
    {
        Slice<T> slice { .pData = static_cast<T*>(arr.pData), .Size = arr.Size };
        return slice;
    }

    static Slice<T> FromArray(const SizedArray<T>& arr)
    {
        Slice<T> slice { .pData = arr.pData, .Size = arr.Size };
        return slice;
    }

    template <uint32 TSize>
    Slice(StackArray<T, TSize>& stack_arr) : pData(stack_arr.pData), Size(stack_arr.Size)
    {
    }

    Slice(T* ptr, uint32 size) : pData(ptr), Size(size) {}
    Slice(nullptr_t np) : pData(nullptr), Size(0) {}

    Slice(Slice<T>&& other) { (*this) = std::move(other); }

    template <typename TOtherType>
    Slice(const Slice<TOtherType>& other)
    {
        pData = static_cast<T*>(other.pData);
        Size = other.Size;
    }


    Slice& operator=(T* value) = delete;

    Slice& operator=(Slice<T>&& other)
    {
        pData = other.pData;
        Size = other.Size;

        other.pData = nullptr;
        other.Size = 0;

        return *this;
    }

    Slice& operator=(const Slice<T>& other)
    {
        pData = other.pData;
        Size = other.Size;

        return *this;
    }

    bool operator==(nullptr_t np) { return pData == nullptr; }

    operator T*() const { return pData; }

    T& operator[](size_t index)
    {
        if (index > Size) {
            Panic("Slice", "Index out of range! ({:d} > {:d})\n", index, Size);
        }
        return pData[index];
    }

    FX_FORCE_INLINE uint32 GetSizeInBytes() const { return sizeof(T) * Size; }
};

/** Creates a new Slice object */
template <typename T>
Slice<T> MakeSlice(T* ptr, uint32 size)
{
    return Slice<T>(ptr, size);
}

} // namespace fx


template <>
struct std::formatter<fx::Slice<fx::uint8>>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(const fx::Slice<fx::uint8>& slice, std::format_context& ctx) const
    {
        std::string str = "";

        for (fx::uint32 i = 0; i < slice.Size; i++) {
            const fx::uint8& value = slice.pData[i];
            if (i == slice.Size - 1) {
                str += std::format("{:x}", static_cast<unsigned int>(value));
            }
            else {
                str += std::format("{:x}, ", static_cast<unsigned int>(value));
            }
        }

        return std::format_to(ctx.out(), "[ {} ]", str);
    }
};
