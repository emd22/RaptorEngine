#pragma once

#include "Assert.hpp"

#include <Core/Types.hpp>
#include <atomic>
#include <mutex>

template <typename TObjectType>
class LockContext
{
public:
    static LockContext NoLock(std::mutex& mutex, TObjectType& object)
    {
        LockContext ctx(mutex, object, true);
        return ctx;
    }

    FX_FORCE_INLINE LockContext(std::mutex& mutex, TObjectType& object, bool never_lock = false)
        : mMutex(mutex), mObject(object)
    {
        if (!never_lock) {
            mMutex.lock();
            mbIsLocked = true;
        }
    }

    FX_FORCE_INLINE TObjectType& Get() { return mObject; }
    FX_FORCE_INLINE TObjectType* operator->() { return &mObject; }

    FX_FORCE_INLINE void Unlock()
    {
        if (!mbIsLocked) {
            return;
        }

        mbIsLocked = false;
        mMutex.unlock();
    }

    ~LockContext() { Unlock(); }


private:
    std::mutex& mMutex;
    TObjectType& mObject;
    bool mbIsLocked = false;
};


template <typename TObjectType>
class SpinLockContext
{
public:
    FX_FORCE_INLINE SpinLockContext(std::atomic_flag& af, TObjectType& object, bool never_lock = false)
        : mAtomicFlag(af), mObject(object)
    {
        if (never_lock) {
            return;
        }

        while (af.test_and_set()) {
            af.wait(true);
        }

        mbIsLocked = true;
    }

    SpinLockContext(const SpinLockContext& other) = delete;

    FX_FORCE_INLINE SpinLockContext(SpinLockContext&& other) noexcept
        : mAtomicFlag(other.mAtomicFlag), mObject(other.mObject), mbIsLocked(other.mbIsLocked)
    {
        other.mbIsLocked = false;
    }

    FX_FORCE_INLINE TObjectType& Get() { return mObject; }
    FX_FORCE_INLINE TObjectType* operator->() { return &mObject; }

    SpinLockContext& operator=(const SpinLockContext& other) = delete;

    FX_FORCE_INLINE void Unlock()
    {
        if (!mbIsLocked) {
            return;
        }

        mAtomicFlag.clear();
        mAtomicFlag.notify_one();

        mbIsLocked = false;
    }

    ~SpinLockContext() { Unlock(); }


private:
    std::atomic_flag& mAtomicFlag;
    TObjectType& mObject;

    bool mbIsLocked = false;
};


class SpinLockGuard
{
public:
    FX_FORCE_INLINE SpinLockGuard(std::atomic_flag& af) : mAtomicFlag(af)
    {
        while (af.test()) {
            af.wait(true);
        }

        af.test_and_set();

        mbIsLocked = true;
    }

    SpinLockGuard(const SpinLockGuard& other) = delete;

    FX_FORCE_INLINE SpinLockGuard(SpinLockGuard&& other) noexcept : mAtomicFlag(other.mAtomicFlag)
    {
        other.mbIsLocked = false;
    }

    SpinLockGuard& operator=(const SpinLockGuard& other) = delete;

    FX_FORCE_INLINE void Unlock()
    {
        if (!mbIsLocked) {
            return;
        }

        mAtomicFlag.clear();
        mAtomicFlag.notify_one();

        mbIsLocked = false;
    }

    ~SpinLockGuard() { Unlock(); }


private:
    std::atomic_flag& mAtomicFlag;
    bool mbIsLocked = false;
};
