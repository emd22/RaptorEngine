#pragma once

#include <condition_variable>
#include <mutex>

class DataNotifier
{
public:
    DataNotifier() = default;

    void Signal()
    {
        std::lock_guard<std::mutex> lock(mMutex);

        if (mKilled) {
            return;
        }

        mDone = true;


        // Notify the other thread
        mCV.notify_one();
    }

    void Wait(bool pass_if_already_done = false)
    {
        std::unique_lock<std::mutex> lock(mMutex);

        if (pass_if_already_done && mDone) {
            return;
        }

        mCV.wait(lock, [this] { return mDone; });
    }

    void WaitAndReset()
    {
        std::unique_lock<std::mutex> lock(mMutex);

        if (mDone) {
            mKilled = false;
            mDone = false;
            return;
        }

        mCV.wait(lock, [this] { return mDone; });

        mKilled = false;
        mDone = false;
    }

    bool IsDone()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mDone;
    }

    void Reset()
    {
        std::lock_guard<std::mutex> lock(mMutex);

        mKilled = false;
        mDone = false;
    }

    bool IsKilled()
    {
        std::lock_guard<std::mutex> lock(mMutex);

        return mKilled;
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
