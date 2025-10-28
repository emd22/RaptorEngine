#pragma once

#include "FxSizedArray.hpp"
#include "FxTypes.hpp"

class FxBitset
{
public:
    static constexpr int scNoFreeBits = -1;

public:
    FxBitset() = default;
    explicit FxBitset(uint32 max_bits) { InitZero(max_bits); }

    void InitZero(uint32 max_bits);
    void InitOne(uint32 max_bits);

    FX_FORCE_INLINE int FindNextFreeBit() const;

    FX_FORCE_INLINE void Set(uint32 index);
    FX_FORCE_INLINE bool Get(uint32 index);
    FX_FORCE_INLINE void Unset(uint32 index);

    void Print();

private:
    uint32 Init(uint32 max_bits);

private:
    FxSizedArray<uint64> mBits;
};

// Inline definitions
#include "FxBitset.inl"
