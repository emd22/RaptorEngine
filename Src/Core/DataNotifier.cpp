/*
 * File:        DataNotifier.cpp
 * Author:      emd22
 * Created:     17/06/2026
 * Description: Provides basic signal/reciever helpers to deal with cross-thread data easier.
 */

#include "DataNotifier.hpp"


namespace fx {

/////////////////////////////////////
// Data Notifier
/////////////////////////////////////

void DataNotifier::Signal()
{
    std::lock_guard<std::mutex> lock(mMutex);

    if (mbIsKilled) {
        return;
    }

    mbIsSignalled = true;

    // Notify the other thread
    mCV.notify_one();
}

void DataNotifier::Wait(bool pass_if_already_done)
{
    std::unique_lock<std::mutex> lock(mMutex);

    if (pass_if_already_done && mbIsSignalled) {
        return;
    }

    mCV.wait(lock, [this] { return mbIsSignalled; });
}

void DataNotifier::WaitAndReset()
{
    std::unique_lock<std::mutex> lock(mMutex);

    if (mbIsSignalled) {
        mbIsKilled = false;
        mbIsSignalled = false;
        return;
    }

    mCV.wait(lock, [this] { return mbIsSignalled; });

    mbIsKilled = false;
    mbIsSignalled = false;
}


void DataNotifier::Reset()
{
    std::lock_guard<std::mutex> lock(mMutex);

    mbIsKilled = false;
    mbIsSignalled = false;
}


void DataNotifier::Kill()
{
    std::lock_guard<std::mutex> lock(mMutex);

    mbIsKilled = true;
    mbIsSignalled = true;

    mCV.notify_one();
}

bool DataNotifier::IsSignalled() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mbIsSignalled;
}

bool DataNotifier::IsKilled() const
{
    std::lock_guard<std::mutex> lock(mMutex);

    return mbIsKilled;
}

/////////////////////////////////////
// Counted Notifier
/////////////////////////////////////


void CountedNotifier::Signal()
{
    std::lock_guard<std::mutex> lock(mMutex);

    if (mbIsKilled) {
        return;
    }

    ++mNumberOfSignals;

    // Notify the other thread
    mCV.notify_one();
}

// void CountedNotifier::Wait(bool pass_if_already_done)
// {
//     std::unique_lock<std::mutex> lock(mMutex);

//     if (pass_if_already_done && mbIsSignalled) {
//         return;
//     }

//     mCV.wait(lock, [this] { return mbIsSignalled; });
// }

void CountedNotifier::Wait()
{
    std::unique_lock<std::mutex> lock(mMutex);

    if (mNumberOfSignals == 0) {
        mCV.wait(lock, [this] { return mNumberOfSignals > 0; });
    }

    mbIsKilled = false;
    --mNumberOfSignals;
}

void CountedNotifier::Discard()
{
    std::unique_lock<std::mutex> lock(mMutex);

    if (mNumberOfSignals > 0) {
        --mNumberOfSignals;
        return;
    }
}

void CountedNotifier::Reset()
{
    std::lock_guard<std::mutex> lock(mMutex);

    mbIsKilled = false;
    mNumberOfSignals = 0;
}

bool CountedNotifier::IsKilled() const
{
    std::lock_guard<std::mutex> lock(mMutex);

    return mbIsKilled;
}

void CountedNotifier::Kill()
{
    std::lock_guard<std::mutex> lock(mMutex);

    mbIsKilled = true;
    mNumberOfSignals = 1000;

    mCV.notify_one();
}

int CountedNotifier::GetSignalCount() const
{
    std::lock_guard<std::mutex> lock(mMutex);

    return mNumberOfSignals;
}


} // namespace fx
