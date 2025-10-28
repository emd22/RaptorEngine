#pragma once

#include "FxTypes.hpp"

#include <bit>

namespace FxBit {

/**
 * @brief Gets the least significant bit for an integer `v_`.
 */
#define FX_BIT_GET_LSB(v_) ((v_) & -(v_))

template <typename TIntType>
FX_FORCE_INLINE TIntType FindFirstOne_Fallback(TIntType v)
{
    constexpr TIntType cIntWidth = sizeof(TIntType) * 8;

    // Get the least significant bit isolated for v
    TIntType lsb_v = FX_BIT_GET_LSB(v);

    return (cIntWidth - std::countl_zero(lsb_v)) - static_cast<TIntType>(1);
}


template <typename TIntType>
FX_FORCE_INLINE TIntType FindFirstOne(TIntType v)
{
    return FindFirstOne_Fallback<TIntType>(v);
}


template <>
FX_FORCE_INLINE uint64 FindFirstOne<uint64>(uint64 v)
{
#if defined(FX_COMPILER_CLANG) || defined(FX_COMPILER_GCC)
    return static_cast<uint64>(__builtin_ffsll(v)) - 1ULL;
#else
    // TODO: [Windows] add _BitScanForward64() here and TEST IT
    return FindFirstOne_Fallback<uint64>(v);
#endif
}

template <>
FX_FORCE_INLINE uint32 FindFirstOne<uint32>(uint32 v)
{
#if defined(FX_COMPILER_CLANG) || defined(FX_COMPILER_GCC)
    return static_cast<uint64>(__builtin_ffs(v)) - 1U;
#else
    // TODO: [Windows] add _BitScanForward64() here and TEST IT
    return FindFirstOne_Fallback<uint32>(v);
#endif
}


template <typename TIntType>
FX_FORCE_INLINE TIntType FindFirstZero(TIntType v)
{
    return FindFirstOne<TIntType>(~v);
}


} // namespace FxBit
