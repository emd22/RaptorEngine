/*
 * File:        Random.hpp
 * Author:      emd22
 * Created:     24/08/2026
 * Description: Functions and helpers for generating random values
 */

#pragma once

#include "Types.hpp"

#include <Math/SIMDHelper.hpp>

namespace fx {

/**
 * @brief Generates a 32-bit random number using Xorshift.
 */
uint32 FastRand32();

/**
 * @brief Restarts FastRand32() from a seed, for runs that must repeat exactly. A seed of zero is replaced, since
 * Xorshift never leaves zero.
 */
void SeedFastRand(uint32 seed);

/**
 * @brief Generates a 64-bit random number using Xorshift.
 */
uint64 FastRand64();

/**
 * @brief Generates a random float in [0, 1) from FastRand32().
 */
float32 FastRandUnit();


/////////////////////////////////////
// SIMD Functions
/////////////////////////////////////

/**
 * @brief Generates 4 PRNG values, returned in a platform specific vector.
 */
UINT4 FastRand4();

} // namespace fx
