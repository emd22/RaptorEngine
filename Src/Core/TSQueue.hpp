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

    TSQueue(uint32 num_objects)
    {
        SpinLockGuard guard(mLock);
        mQueue.InitCapacity(num_objects);
    }

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

    SpinLockContext<Queue<T>> GetQueue() { return std::move(SpinLockContext(mLock, mQueue)); }

    ~TSQueue()
    {
        SpinLockGuard guard(mLock);
        mQueue.Destroy();
    }

private:
    Queue<T> mQueue;
    mutable std::atomic_flag mLock;
};

} // namespace fx
