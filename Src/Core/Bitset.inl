#pragma once

#include "BitUtil.hpp"
#include "Bitset.hpp"

namespace fx {

FX_FORCE_INLINE void Bitset::Set(uint32 index)
{
    if (mBits.IsEmpty()) {
        return;
    }

    // The index into the int array (index / 64)
    const uint32 int_index = (index >> 6);

    // The mask for the bit that we are querying
    const uint64 mask = (1ULL << (index & scBitIndexMask));

    mBits.pData[int_index] |= mask;
}


FX_FORCE_INLINE bool Bitset::Get(uint32 index) const
{
    if (mBits.IsEmpty()) {
        return false;
    }

    // The index into the int array (index / 64)
    const uint32 int_index = (index >> 6);

    // The mask for the bit that we are querying
    const uint64 mask = (1ULL << (index & scBitIndexMask));

    return (mBits.pData[int_index] & mask);
}

FX_FORCE_INLINE void Bitset::Unset(uint32 index)
{
    if (mBits.IsEmpty()) {
        return;
    }

    // The index into the int array (index / 64)
    const uint32 int_index = (index >> 6);

    // The mask for the bit that we are querying
    const uint64 mask = (1ULL << (index & scBitIndexMask));

    mBits.pData[int_index] &= (~mask);
}

FX_FORCE_INLINE uint32 Bitset::FindNextFreeBit(uint32 start_index) const
{
    const uint32 start_byte = (start_index / scBitsPerInt);

    uint64 byte_mask = 0;

    uint64 bit_index = (start_index & scBitIndexMask);

    // if the start index is not at a byte boundary, we need to mask off the start of the byte.
    if (bit_index != 0) {
        byte_mask = (1ULL << bit_index) - 1;
    }

    constexpr uint64 cFullChunk = (~0ULL);

    for (uint32 i = start_byte; i < mBits.Size; i++) {
        const uint64 chunk = (mBits[i] | byte_mask);

        // Every bit is set, continue to next chunk
        if (chunk == cFullChunk) {
            continue;
        }

        // Find bit in chunk (R to L) and return the index in the bitset.
        // This is returning (current_byte_n * 64) + index_in_byte.

        uint32 bit_index = Bit::FindFirstZero64(chunk);
        DebugAssert(bit_index != Bit::scBitNotFound);

        return (i << 6) + bit_index;
    }

    return scNoFreeBits;
}

FX_FORCE_INLINE uint32 Bitset::FindNextFreeBitGroup(uint32 group_size) const
{
    const uint32 max_size = mBits.Size * scBitsPerInt;

    uint32 start_index = FindNextFreeBit();

    while (start_index < max_size) {
        if (!Get(start_index + group_size)) {
            bool is_free = true;
            for (uint32 i = start_index + 1; i < start_index + group_size - 1; i++) {
                if (Get(i)) {
                    is_free = false;
                    break;
                }
            }

            // Full group is free, break out
            if (is_free) {
                return start_index;
            }
        }

        start_index = FindNextFreeBit(start_index);
    }

    return Bit::scBitNotFound;
}

FX_FORCE_INLINE uint64 Bitset::GetBitCapacity() const { return mBits.Size * scBitsPerInt; }


} // namespace fx
