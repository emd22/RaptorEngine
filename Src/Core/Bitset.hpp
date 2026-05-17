#pragma once

#include "SizedArray.hpp"
#include "Types.hpp"

namespace fx {

class Bitset
{
public:
    static constexpr uint32 scNoFreeBits = UINT32_MAX;

    /// Mask bits that are outside of a byte boundary
    static constexpr uint32 scBitIndexMask = 0x3F;

public:
    Bitset() = default;
    explicit Bitset(uint32 max_bits) { InitZero(max_bits); }

    void InitZero(uint32 max_bits);
    void InitOne(uint32 max_bits);

    /**
     * @brief Finds the next zero bit in the bitset
     * @return An index to the bit, or `Bitset::scNoFreeBits` if there are none remaining.
     */
    FX_FORCE_INLINE uint32 FindNextFreeBit(uint32 start_index = 0) const;
    FX_FORCE_INLINE uint32 FindNextFreeBitGroup(uint32 group_size) const;

    FX_FORCE_INLINE void Set(uint32 index);
    FX_FORCE_INLINE bool Get(uint32 index) const;
    FX_FORCE_INLINE void Unset(uint32 index);

    /**
     * @brief Sets all bits to zero.
     */
    void ClearAll();

    void Print();

private:
    uint32 Init(uint32 max_bits);

private:
    SizedArray<uint64> mBits;
};

} // namespace fx

// Inline definitions
#include "Bitset.inl"
