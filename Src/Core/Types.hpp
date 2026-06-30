#pragma once

#include "Defines.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>


#ifndef FX_SPIN_THREAD_GUARD_DEBUG_USE_MUTEX
#include <atomic>
using AtomicFlag = std::atomic_flag;
#else
#include <mutex>
using AtomicFlag = std::mutex;
#endif

namespace fx {

using int8 = int8_t;
using int16 = int16_t;
using int32 = int32_t;
using int64 = int64_t;

using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

using float32 = float;
using float64 = double;


constexpr uint64 UnitByte = 1;
constexpr uint64 UnitKibibyte = 1024;
constexpr uint64 UnitMebibyte = UnitKibibyte * 1024;
constexpr uint64 UnitGibibyte = UnitMebibyte * 1024;

using Handle = uint32;

static constexpr Handle NullHandle = UINT32_MAX;


template <typename T, typename... TTypes>
concept C_IsAnyOf = (std::is_same_v<T, TTypes> || ...);


template <typename T, typename... TTypes>
concept C_IsAnyBaseOf = (std::is_base_of_v<TTypes, T> || ...);

template <typename TPossiblyConst, typename TNonConst>
concept C_IsSameOrConst = std::is_same_v<typename std::remove_const<TPossiblyConst>::type, TNonConst>;


/////////////////////////////////////
// Enum class bit flags
/////////////////////////////////////

#define FxEnumFlags(type_)                                                                                             \
    template <>                                                                                                        \
    struct EnumFlagsOptIn<type_>                                                                                       \
    {                                                                                                                  \
        static constexpr bool bValue = true;                                                                           \
    }


template <typename T>
struct EnumFlagsOptIn
{
    static constexpr bool bValue = false;
};

template <typename T>
concept C_IsEnumFlags = std::is_enum_v<T> && EnumFlagsOptIn<T>::bValue;

template <typename T>
using EnumFlagsIntType = typename std::underlying_type<T>::type;


template <typename T>
    requires C_IsEnumFlags<T>
constexpr T operator&(T lhs, T rhs)
{
    using InternalType = EnumFlagsIntType<T>;
    return static_cast<T>(static_cast<InternalType>(lhs) & static_cast<InternalType>(rhs));
}

template <typename T>
    requires C_IsEnumFlags<T>
constexpr T& operator&=(T& lhs, T rhs)
{
    using InternalType = EnumFlagsIntType<T>;
    lhs = static_cast<T>(static_cast<InternalType>(lhs) & static_cast<InternalType>(rhs));

    return lhs;
}

template <typename T>
    requires C_IsEnumFlags<T>
constexpr T operator|(T lhs, T rhs)
{
    using InternalType = EnumFlagsIntType<T>;
    return static_cast<T>(static_cast<InternalType>(lhs) | static_cast<InternalType>(rhs));
}

template <typename T>
    requires C_IsEnumFlags<T>
constexpr T& operator|=(T& lhs, T rhs)
{
    using InternalType = EnumFlagsIntType<T>;
    lhs = static_cast<T>(static_cast<InternalType>(lhs) | static_cast<InternalType>(rhs));
    return lhs;
}

template <typename T>
    requires C_IsEnumFlags<T>
constexpr T& operator^=(T& lhs, T rhs)
{
    using InternalType = EnumFlagsIntType<T>;
    lhs = static_cast<T>(static_cast<InternalType>(lhs) ^ static_cast<InternalType>(rhs));
    return lhs;
}

template <typename T>
    requires C_IsEnumFlags<T>
constexpr bool operator!=(T lhs, int rhs)
{
    return (static_cast<EnumFlagsIntType<T>>(lhs)) != rhs;
}

template <typename T>
    requires C_IsEnumFlags<T>
constexpr bool operator==(T lhs, int rhs)
{
    return (static_cast<EnumFlagsIntType<T>>(lhs)) == rhs;
}

template <typename T>
constexpr T operator~(T v)
{
    return static_cast<T>(~(static_cast<EnumFlagsIntType<T>>(v)));
}

template <typename T>
    requires C_IsEnumFlags<T>
constexpr T& ClearFlag(T& lhs, T flag)
{
    return (lhs &= static_cast<T>(~(static_cast<EnumFlagsIntType<T>>(flag))));
}

template <typename T>
    requires C_IsEnumFlags<T>
constexpr T& SetFlag(T& lhs, T flag)
{
    return (lhs |= flag);
}

template <typename T>
constexpr bool IsFlagSet(T lhs, T flag)
{
    using InternalType = EnumFlagsIntType<T>;
    return (static_cast<InternalType>(lhs) & static_cast<InternalType>(flag)) != 0;
}

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
//             Panic("Optional", "Could not retrieve undefined value", 0);
//         }

//         return *reinterpret_cast<ValueType *>(&mData);
//     }

//     ValueType &Get() const { return *this; }

// private:
//     bool mHasValue = false;
//     std::aligned_storage<sizeof(ValueType), std::alignment_of<ValueType>::value> mData;
// };


// #define FX_SPIN_THREAD_GUARD_DEBUG_USE_MUTEX 1


#define FX_ENUM_CASE_NAME(member_)                                                                                     \
    case ENUM_TYPE::member_:                                                                                           \
        return #member_

struct SpinThreadGuard
{
public:
    SpinThreadGuard(AtomicFlag* busy_flag) noexcept
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

    void Unlock() noexcept {}

    ~SpinThreadGuard() noexcept
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
    AtomicFlag* mFlag = nullptr;
#endif
};


} // namespace fx
