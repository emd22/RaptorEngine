#pragma once

#include <mutex>
#include <deque>

#include "FxAssetQueueItem.hpp"

class FxAssetQueue
{
public:
    FxAssetQueue() = default;

    void Push(FxAssetQueueItem &value)
    {
        std::lock_guard<std::mutex> lock(mMutex);

        mQueue.push_back(value);
    }

    // void Push(FxAssetQueueItem &&value)
    // {
    //     std::lock_guard<std::mutex> lock(mMutex);

    //     mQueue.push_back(std::move(value));
    // }

    // void Push(FxAssetQueueItem value)
    // {
    //     std::lock_guard<std::mutex> lock(mMutex);

    //     mQueue.push_back(value);
    // }

    bool PopIfAvailable(FxAssetQueueItem *item)
    {
        std::lock_guard<std::mutex> lock(mMutex);

        if (mQueue.empty()) {
            return false;
        }

        FxAssetQueueItem &queue_item = mQueue.front();
        (*item) = std::move(queue_item);

        mQueue.pop_front();

        return true;
    }

    void Destroy()
    {
        std::lock_guard<std::mutex> lock(mMutex);

        mQueue.clear();
    }

private:
    std::deque<FxAssetQueueItem> mQueue;

    std::mutex mMutex;
};
