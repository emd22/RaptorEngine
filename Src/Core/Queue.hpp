#pragma once

#include "Assert.hpp"
#include "Log.hpp"
#include "Types.hpp"

#include <Math/MathUtil.hpp>

namespace fx {

template <typename T>
class Queue
{
public:
    Queue() = default;
    Queue(uint32 num_objects) { InitCapacity(num_objects); }

    Queue(const Queue& other) = delete;
    Queue(Queue&& other) { (*this) = std::move(other); }

    Queue& operator=(const Queue& other) = delete;
    Queue& operator=(Queue&& other)
    {
        mpData = other.mpData;
        mCapacity = other.mCapacity;
        mSize = other.mSize;
        mPushIndex = other.mPushIndex;
        mPopIndex = other.mPopIndex;

        other.mCapacity = 0;
        other.mpData = nullptr;

        return *this;
    }

    bool IsInited() const { return mpData != nullptr; }

    void InitCapacity(uint32 num_objects)
    {
        Assert(num_objects > 0);

        if (mpData != nullptr) {
            // Recreate queue
            Destroy();
        }

        mCapacity = num_objects;
        mPushIndex = 0;
        mPopIndex = 0;
        mSize = 0;

        const uint32 buffer_size = sizeof(T) * num_objects;
        mpData = reinterpret_cast<T*>(std::malloc(buffer_size));

        if (mpData == nullptr) {
            LogError(LC_CORE, "Could not allocate memory for queue of size {}", buffer_size);
            mCapacity = 0;
        }
    }

    T& Push(const T& value)
    {
        if (mSize >= mCapacity) {
            LogWarning("Queue is full");
            Pop();
        }

        if (mPushIndex >= mCapacity) {
            mPushIndex = 0;
        }

        ++mSize;

        T* ptr = mpData + (mPushIndex++);
        new (ptr) T(value);

        return *ptr;
    }

    T& Push(T&& value)
    {
        if (mSize >= mCapacity) {
            LogWarning("Queue is full");
            Pop();
        }

        if (mPushIndex >= mCapacity) {
            mPushIndex = 0;
        }

        ++mSize;

        T* ptr = mpData + (mPushIndex++);
        new (ptr) T(std::move(value));

        return *ptr;
    }

    template <typename... TArgs>
    T& Emplace(TArgs&&... args)
    {
        if (mSize >= mCapacity) {
            LogWarning("Queue is full");
            Pop();
        }

        if (mPushIndex >= mCapacity) {
            mPushIndex = 0;
        }

        ++mSize;

        T* ptr = mpData + (mPushIndex++);
        new (ptr) T(std::forward<TArgs>(args)...);

        return *ptr;
    }

    T PopValue()
    {
        Assert(mSize > 0);

        if (mPopIndex >= mCapacity) {
            mPopIndex = 0;
        }

        --mSize;

        T* ptr = mpData + (mPopIndex++);

        T value = std::move(*ptr);
        ptr->~T();

        return value;
    }

    T& First() const
    {
        Assert(mSize > 0);
        T* ptr = mpData + (mPopIndex);

        return *ptr;
    }

    void Pop()
    {
        if (mSize == 0) {
            LogError(LC_CORE, "Cannot pop from empty queue");
            return;
        }

        if (mPopIndex >= mCapacity) {
            mPopIndex = 0;
        }

        --mSize;

        T* ptr = mpData + (mPopIndex++);
        if constexpr (!std::is_trivially_destructible_v<T>) {
            ptr->~T();
        }
    }

    bool IsEmpty() const { return mSize == 0; }


    void Destroy()
    {
        if (!mpData) {
            return;
        }

        // Destroy all items remaining
        for (uint32 index = 0; index < mSize; index++) {
            Pop();
        }

        std::free(static_cast<void*>(mpData));
        mpData = nullptr;
        mCapacity = 0;
    }

    ~Queue() = default;


private:
    T* mpData = nullptr;

    uint32 mCapacity = 0;
    uint32 mSize = 0;

    uint32 mPushIndex = 0;
    uint32 mPopIndex = 0;
};
} // namespace fx
