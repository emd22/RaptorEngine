#pragma once

#include <stdint.h>

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef float float32;
typedef double float64;


#include <cstddef>
// #include <type_traits>

#include "FxDefines.hpp"

#include <Core/FxPanic.hpp>


// template <typename ValueType>
// class Optional {
// public:
//     Optional(ValueType &value)
//         : mHasValue(true),
//           mData()
//     {
//     }

//     Optional(std::nullptr_t)
//         : mHasValue(false),
//           mData()
//     {
//     }

//     bool HasValue() const { return mHasValue; }

//     void Clear()
//     {
//         mHasValue = false;
//     }

//     ValueType &operator *() const
//     {
//         if (!HasValue()) {
//             FxPanic("Optional", "Could not retrieve undefined value", 0);
//         }

//         return *reinterpret_cast<ValueType *>(&mData);
//     }

//     ValueType &Get() const { return *this; }

// private:
//     bool mHasValue = false;
//     std::aligned_storage<sizeof(ValueType), std::alignment_of<ValueType>::value> mData;
// };


// #define FX_SPIN_THREAD_GUARD_DEBUG_USE_MUTEX 1

#ifndef FX_SPIN_THREAD_GUARD_DEBUG_USE_MUTEX
#include <atomic>
using FxAtomicFlag = std::atomic_flag;
#else
#include <mutex>
using FxAtomicFlag = std::mutex;
#endif

struct FxSpinThreadGuard
{
public:
    FxSpinThreadGuard(FxAtomicFlag* busy_flag) noexcept
    {
#ifdef FX_SPIN_THREAD_GUARD_DEBUG_USE_MUTEX
        mMutex = busy_flag;

        mMutex->lock();
#else
        mFlag = busy_flag;

        // If the busy flag is set currently, wait until it is cleared
        while (mFlag->test()) {
            mFlag->wait(true);
        }

        // Mark as busy
        mFlag->test_and_set();
        mFlag->notify_one();
#endif
    }

    ~FxSpinThreadGuard() noexcept
    {
#ifdef FX_SPIN_THREAD_GUARD_DEBUG_USE_MUTEX
        mMutex->unlock();
#else
        mFlag->clear(std::memory_order_release);
        mFlag->notify_one();
#endif
    }

private:
#ifdef FX_SPIN_THREAD_GUARD_DEBUG_USE_MUTEX
    std::mutex* mMutex;
#else
    FxAtomicFlag* mFlag = nullptr;
#endif
};
