#include "Bitset.hpp"

namespace fx {


uint32 Bitset::Init(uint32 max_bits)
{
    // 0 0 1 1 1 1 1 1 -> 63 -> 3F
    const uint8 remainder = (max_bits & 0x3F);

    // Adjust the amount of bits to be a multiple of 64
    if (remainder != 0) {
        max_bits += (scBitsPerInt - remainder);
    }

    const uint32 ints_required = (max_bits >> 6);

    mBits.InitSize(ints_required);

    return ints_required;
}


void Bitset::InitZero(uint32 max_bits)
{
    uint32 num_ints = Init(max_bits);

    for (uint32 i = 0; i < num_ints; i++) {
        mBits[i] = 0;
    }
}

void Bitset::InitOne(uint32 max_bits)
{
    uint32 num_ints = Init(max_bits);

    for (uint32 i = 0; i < num_ints; i++) {
        mBits[i] = ~0ULL;
    }
}

void Bitset::ClearAll() { memset(mBits.pData, 0, mBits.GetCapacityInBytes()); }

static constexpr uint8 GetBit(uint8 byte, uint8 bit) { return ((byte >> bit) & 0x01); }

static void PrintByte(uint8 b)
{
    printf("%d%d%d%d %d%d%d%d ", GetBit(b, 7), GetBit(b, 6), GetBit(b, 5), GetBit(b, 4), GetBit(b, 3), GetBit(b, 2),
           GetBit(b, 1), GetBit(b, 0));
}


void Bitset::Print() const
{
    uint32 size = static_cast<uint32>(mBits.Size);

    for (int i = 0; i < size; i++) {
        uint64 value = mBits.pData[i];

        printf("Bits(%d): ", i + 1);

        PrintByte(static_cast<uint8>(value >> 56));
        PrintByte(static_cast<uint8>(value >> 48));
        PrintByte(static_cast<uint8>(value >> 40));
        PrintByte(static_cast<uint8>(value >> 32));

        PrintByte(static_cast<uint8>(value >> 24));
        PrintByte(static_cast<uint8>(value >> 16));
        PrintByte(static_cast<uint8>(value >> 8));
        PrintByte(static_cast<uint8>(value >> 0));

        printf("\n");
    }
}

} // namespace fx
