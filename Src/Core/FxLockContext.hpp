#pragma once

#include "FxPanic.hpp"

#include <Core/FxTypes.hpp>
#include <atomic>
#include <mutex>

template <typename TObjectType>
class FxLockContext
{
public:
    static FxLockContext NoLock(std::mutex& mutex, TObjectType& object)
    {
        FxLockContext ctx(mutex, object, true);
        return ctx;
    }

    FX_FORCE_INLINE FxLockContext(std::mutex& mutex, TObjectType& object, bool never_lock = false)
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

    ~FxLockContext() { Unlock(); }


private:
    std::mutex& mMutex;
    TObjectType& mObject;
    bool mbIsLocked = false;
};


template <typename TObjectType>
class FxSpinLockContext
{
public:
    FX_FORCE_INLINE FxSpinLockContext(std::atomic_flag& af, TObjectType& object, bool never_lock = false)
        : mAtomicFlag(af), mObject(object)
    {
        if (never_lock) {
            return;
        }

        while (af.test()) {
            af.wait(true);
        }

        af.test_and_set();

        mbIsLocked = true;
    }

    FX_FORCE_INLINE TObjectType& Get() { return mObject; }

    FX_FORCE_INLINE TObjectType* operator->() { return &mObject; }

    FX_FORCE_INLINE void Unlock()
    {
        if (!mbIsLocked) {
            return;
        }

        mAtomicFlag.clear();
        mAtomicFlag.notify_one();

        mbIsLocked = false;
    }

    ~FxSpinLockContext() { Unlock(); }


private:
    std::atomic_flag& mAtomicFlag;
    TObjectType& mObject;

    bool mbIsLocked = false;
};
