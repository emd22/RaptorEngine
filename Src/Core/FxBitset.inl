#pragma once

#include "FxBitset.hpp"

#include <bit>

FX_FORCE_INLINE void FxBitset::Set(uint32 index)
{
    // The index into the int array (index / 64)
    const uint16 int_index = (index >> 6);

    // The mask for the bit that we are querying
    const uint64 mask = (1ULL << (index & 0x3F));

    mBits.Data[int_index] |= mask;
}


FX_FORCE_INLINE bool FxBitset::Get(uint32 index)
{
    // The index into the int array (index / 64)
    const uint16 int_index = (index >> 6);

    // The mask for the bit that we are querying
    const uint64 mask = (1ULL << (index & 0x3F));

    return (mBits.Data[int_index] & mask);
}

FX_FORCE_INLINE void FxBitset::Unset(uint32 index)
{
    // The index into the int array (index / 64)
    const uint16 int_index = (index >> 6);

    // The mask for the bit that we are querying
    const uint64 mask = (1ULL << (index & 0x3F));

    mBits.Data[int_index] &= (~mask);
}

FX_FORCE_INLINE int FxBitset::FindNextFreeBit() const
{
    const uint64 c_full_chunk = (~0ULL);

    for (uint32 i = 0; i < mBits.Size; i++) {
        const uint64 chunk = mBits[i];

        // Every bit is set, continue to next chunk
        if (chunk == c_full_chunk) {
            continue;
        }

        // Find bit in chunk (R to L) and return the index in the bitset.
        // This is returning (current_byte_n * 64) + index_in_byte.

        return (i << 6) + std::countr_zero(chunk);
    }

    return scNoFreeBits;
}
