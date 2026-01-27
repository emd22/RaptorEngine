#pragma once

#include <Core/FxDefines.hpp>

#ifdef FX_USE_SIMDE
#include <simde/avx2.h>
#include <simde/fma.h>
#include <simde/sse4.2.h>
#else
#include <immintrin.h>
#endif
