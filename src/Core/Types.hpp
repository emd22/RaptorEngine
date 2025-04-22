#pragma once

#include <stdint.h>

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef float float32;
typedef double float64;

#include <cstddef>
#include <type_traits>

#include <Core/FxPanic.hpp>

template <typename ValueType>
class Optional {
public:
    Optional(ValueType &value)
        : mHasValue(true),
          mData()
    {
    }

    Optional(std::nullptr_t)
        : mHasValue(false),
          mData()
    {
    }

    bool HasValue() const { return mHasValue; }

    void Clear()
    {
        mHasValue = false;
    }

    ValueType &operator *() const
    {
        if (!HasValue()) {
            FxPanic_("Optional", "Could not retrieve undefined value", 0);
        }

        return *reinterpret_cast<ValueType *>(&mData);
    }

    ValueType &Get() const { return *this; }

private:
    bool mHasValue = false;
    std::aligned_storage<sizeof(ValueType), std::alignment_of<ValueType>::value> mData;
};
