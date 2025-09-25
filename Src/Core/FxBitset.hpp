#pragma once

#include "FxSizedArray.hpp"
#include "FxTypes.hpp"

class FxBitset
{
public:
    void InitZero(uint32 max_bits);
    void InitOne(uint32 max_bits);

    bool Get(uint32 index);

    void Set(uint32 index);
    void Unset(uint32 index);

    void Print();

private:
    uint32 Init(uint32 max_bits);

private:
    FxSizedArray<uint64> mBits;
};
