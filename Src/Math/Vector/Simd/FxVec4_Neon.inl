#pragma once

#include <Math/FxVec4.hpp>

#ifdef FX_USE_NEON

FX_FORCE_INLINE float32 FxVec4f::LengthSquared() const
{
    // Preload into register
    float32x4_t vec = mIntrin;

    vec = vmulq_f32(vec, vec);

    // Calculate the length squared by doing a horizontal add (add all components)
    return vaddvq_f32(vec);
}

#endif
