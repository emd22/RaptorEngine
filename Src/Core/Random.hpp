/*
 * File:        Random.hpp
 * Author:      emd22
 * Created:     24/08/2026
 * Description: Functions and helpers for generating random values
 */

#pragma once

#include "Types.hpp"

namespace fx {

/**
 * @brief Generates a 32-bit random number using Xorshift.
 */
uint32 FastRand32();

/**
 * @brief Generates a 64-bit random number using Xorshift.
 */
uint64 FastRand64();


} // namespace fx
