#pragma once

#include <mutex>
#include <condition_variable>

#include <Core/Log.hpp>

class FxDataNotifier
{
public:
    void SignalDataWritten()
    {
        std::lock_guard<std::mutex> lock(mMutex);

        if (mKilled) {
            return;
        }

        mDone = true;

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
        std::lock_guard<std::mutex> lock(mMutex);
        mKilled = false;
        mDone = false;
    }

    void Kill()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mKilled = true;
        mDone = true;

        mCV.notify_one();
    }

private:
    bool mDone = false;
    bool mKilled = false;

    std::condition_variable mCV;
    std::mutex mMutex;
};
