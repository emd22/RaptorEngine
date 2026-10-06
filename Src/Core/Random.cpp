#include "Random.hpp"

#include <raptor_ffi.h>

namespace fx {

uint32 FastRand32() { return rx_rand32(); }

float32 RandomUnit() { return rx_random_unit(); }

float32 RandomSignedUnit() { return rx_random_signed_unit(); }

} // namespace fx
