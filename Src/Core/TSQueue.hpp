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
		if (this == &other)
			return *this;

		std::atomic_flag* a = &mLock;
		std::atomic_flag* b = &other.mLock;

		if (a > b) {
			std::swap(a, b);
		}
		// Manual lock ordering - acquire first then second
		while (a->test_and_set()) {
			a->wait(true);
		}
		while (b->test_and_set()) {
			b->wait(true);
		}
		mQueue = std::move(other.mQueue);
		b->clear();
		b->notify_one();
		a->clear();
		a->notify_one();
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
