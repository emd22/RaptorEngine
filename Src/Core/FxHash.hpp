#pragma once

#include "Types.hpp"

#define FX_HASH_FNV1A_SEED 0x811C9DC5
#define FX_HASH_FNV1A_PRIME 0x01000193

using FxHash = uint32;

/**
 * Hashes a string at compile time using FNV-1a.
 *
 * Source to algorithm: http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param
 */
inline constexpr FxHash FxHashStr(const char *str)
{
    uint32 hash = FX_HASH_FNV1A_SEED;

    unsigned char ch;
    while ((ch = static_cast<unsigned char>(*(str++)))) {
        hash = (hash ^ ch) * FX_HASH_FNV1A_PRIME;
    }

    return hash;
}

/**
 * Hashes a string at compile time using FNV-1a.
 *
 * Source to algorithm: http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param
 */
inline constexpr FxHash FxHashStr(const char *str, uint32 length)
{
    uint32 hash = FX_HASH_FNV1A_SEED;

    unsigned char ch;
    for (uint32 i = 0; i < length; i++) {
        ch = static_cast<unsigned char>(str[i]);

        if (ch == 0) {
            return hash;
        }

        hash = (hash ^ ch) * FX_HASH_FNV1A_PRIME;
    }

    return hash;
}
