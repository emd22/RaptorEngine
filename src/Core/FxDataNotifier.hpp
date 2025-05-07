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
        mDone = true;
        mCV.notify_one();
    }

    void WaitForData()
    {
        std::unique_lock<std::mutex> lock(mMutex);
        mCV.wait(lock, [this] { return mDone; });
        Log::Info("Wait ended");
    }

    void Reset()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mDone = false;
    }

private:
    bool mDone = false;
    std::condition_variable mCV;
    std::mutex mMutex;
};
