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
            FxPanic("Optional", "Could not retrieve undefined value", 0);
        }

        return *reinterpret_cast<ValueType *>(&mData);
    }

    ValueType &Get() const { return *this; }

private:
    bool mHasValue = false;
    std::aligned_storage<sizeof(ValueType), std::alignment_of<ValueType>::value> mData;
};

template <typename T>
class PtrContainer {
public:
    PtrContainer()
        : mPtr(nullptr)
    {
    }
    PtrContainer(T *ptr)
        : mPtr(ptr)
    {
    }

    PtrContainer(T &&val)
    {
        mPtr = new T(std::move(val));
    }

    ~PtrContainer()
    {
        delete mPtr;
    }

    PtrContainer(PtrContainer &&other)
        : mPtr(other.mPtr)
    {
        other.mPtr = nullptr;
    }

    PtrContainer<T> &operator = (T &&value)
    {
        delete mPtr;
        mPtr = new T(std::move(value));
        return *this;
    }

    PtrContainer<T> &operator = (PtrContainer<T> &&other)
    {
        delete mPtr;
        mPtr = other.mPtr;
        other.mPtr = nullptr;
        return *this;
    }

    static PtrContainer<T> New()
    {
        return PtrContainer<T>(new T());
    }

    PtrContainer(const PtrContainer<T> &other) = delete;
    PtrContainer(PtrContainer<T> &other) = delete;
    PtrContainer<T> &operator = (PtrContainer<T> &other) = delete;
    PtrContainer<T> &operator = (const PtrContainer<T> &other) = delete;

    T *Get() const { return mPtr; }

    T *operator ->() const { return mPtr; }
    T &operator *() const { return *mPtr; }

private:
    T *mPtr = nullptr;
};
