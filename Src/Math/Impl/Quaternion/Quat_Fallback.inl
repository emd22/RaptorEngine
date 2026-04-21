#pragma once

#include <Math/Quat.hpp>

#ifdef FX_NO_SIMD

FX_FORCE_INLINE bool Quat::IsCloseTo(const Quat& other, const float32 threshold)
{
    const bool dx = abs(X - other.X) > threshold;
    const bool dy = abs(Y - other.Y) > threshold;
    const bool dz = abs(Z - other.Z) > threshold;
    const bool dw = abs(W - other.W) > threshold;

    if (dx || dy || dz || dw) {
        return false;
    }

    return true;
}

#endif
