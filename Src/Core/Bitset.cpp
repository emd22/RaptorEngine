#include "Bitset.hpp"

#include <cstdio>

namespace fx {

static constexpr uint8 GetBit(uint8 byte, uint8 bit) { return ((byte >> bit) & 0x01); }

static void PrintByte(uint8 b)
{
    printf("%d%d%d%d %d%d%d%d ", GetBit(b, 7), GetBit(b, 6), GetBit(b, 5), GetBit(b, 4), GetBit(b, 3), GetBit(b, 2),
           GetBit(b, 1), GetBit(b, 0));
}

void Bitset::Print() const
{
    if (mpSlots == nullptr) {
        return;
    }

    const uint64* words = rx_slots_words(mpSlots);
    const uint32 size = static_cast<uint32>(rx_slots_capacity(mpSlots) / scBitsPerInt);

    for (uint32 i = 0; i < size; i++) {
        const uint64 value = words[i];

        printf("Bits(%d): ", i + 1);

        for (int shift = 56; shift >= 0; shift -= 8) {
            PrintByte(static_cast<uint8>(value >> shift));
        }

        printf("\n");
    }
}

} // namespace fx
