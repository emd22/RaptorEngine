#pragma once

#include <mutex>
#include <condition_variable>

#include <Core/Log.hpp>

class FxDataNotifier
{
public:
    FxDataNotifier() = default;

    void SignalDataWritten()
    {
        std::unique_lock<std::mutex> lock(mMutex);

        if (mKilled) {
            return;
        }

        mDone = true;

        lock.unlock();
        // Notify the other thread
        mCV.notify_one();
    }

    void WaitForData()
    {
        std::unique_lock<std::mutex> lock(mMutex);
        mCV.wait(lock, [this] { return mDone; });

        mDone = false;
    }

    void Reset()
    {
        std::unique_lock<std::mutex> lock(mMutex);

        mKilled = false;
        mDone = false;
    }

    bool IsKilled()
    {
        std::unique_lock<std::mutex> lock(mMutex);

        return mKilled;
    }

    void Kill()
    {
        std::unique_lock<std::mutex> lock(mMutex);

        mKilled = true;
        mDone = true;

        lock.unlock();
        mCV.notify_one();
    }



private:
    bool mDone = false;
    bool mKilled = false;

    std::condition_variable mCV;
    std::mutex mMutex;
};
