#pragma once

#include <condition_variable>
#include <mutex>

namespace fx {

class DataNotifier
{
public:
    DataNotifier() = default;

    /**
     * @brief Signals the waiter that it can continue.
     */
    void Signal();

    /**
     * @brief Waits to receive a signal.
     * @param pass_if_already_done Does not wait if the notifier is already signalled.
     */
    void Wait(bool pass_if_already_done = false);
    void WaitAndReset();

    void Reset();
    void Kill();

    bool IsKilled() const;
    bool IsSignalled() const;


private:
    bool mbIsSignalled = false;
    bool mbIsKilled = false;

    std::condition_variable mCV;
    mutable std::mutex mMutex;
};


class CountedNotifier
{
public:
    CountedNotifier() = default;

    /**
     * @brief Signals the waiter that it can continue.
     */
    void Signal();
    void Wait();
    void Discard();

    void Kill();
    void Reset();

    bool IsKilled() const;
    int GetSignalCount() const;


private:
    bool mbIsKilled = false;

    int mNumberOfSignals = 0;

    std::condition_variable mCV;
    mutable std::mutex mMutex;
};


} // namespace fx
