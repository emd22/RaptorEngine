#pragma once

#include "LockContext.hpp"
#include "Queue.hpp"

#include <atomic>

namespace fx {

template <typename T>
class TSQueue
{
public:
    TSQueue() = default;

    TSQueue(uint32 num_objects) { InitCapacity(num_objects); }

    TSQueue(const TSQueue& other) = delete;
    TSQueue(TSQueue&& other) { (*this) = std::move(other); }

    TSQueue& operator=(const TSQueue& other) = delete;
    TSQueue& operator=(TSQueue&& other)
    {
        SpinLockGuard guard_a(mQueue);
        SpinLockGuard guard_b(other.mQueue);

        mQueue = std::move(other.mQueue);

        return *this;
    }

    void InitCapacity(uint32 num_objects)
    {
        SpinLockGuard guard(mLock);
        mQueue.InitCapacity(num_objects);
    }

    T& Push(const T& value)
    {
        SpinLockGuard guard(mLock);
        return mQueue.Push(value);
    }

    T& Push(T&& value)
    {
        SpinLockGuard guard(mLock);
        return mQueue.Push(std::move(value));
    }

    void Pop()
    {
        SpinLockGuard guard(mLock);
        mQueue.Pop();
    }

    T PopValue()
    {
        SpinLockGuard guard(mLock);
        return std::move(mQueue.PopValue());
    }

    void Destroy()
    {
        SpinLockGuard guard(mLock);
        mQueue.Destroy();
    }

    ~TSQueue() { Destroy(); }

private:
    Queue<T> mQueue;
    std::atomic_flag mLock;
};

} // namespace fx
