#pragma once

#include "Types.hpp"

#define FX_HASH_FNV1A_SEED 0x811C9DC5
#define FX_HASH_FNV1A_PRIME 0x01000193

/**
 * Hashes a string at compile time using FNV-1a.
 *
 * Source to algorithm: http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param
 */
inline constexpr uint32 FxCtHashStr(const char *str)
{
    uint32 hash = FX_HASH_FNV1A_SEED;

    unsigned char ch;
    while ((ch = static_cast<unsigned char>(*(str++)))) {
        hash = (hash ^ ch) * FX_HASH_FNV1A_PRIME;
    }

    return hash;
}

/**
 * Hashes a string using FNV-1a.
 */
uint32 FxHashStr(const char *str);
