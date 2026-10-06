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
 * @brief Generates a random value in [0, 1).
 */
float32 RandomUnit();

/**
 * @brief Generates a random value in [-1, 1].
 */
float32 RandomSignedUnit();


} // namespace fx
