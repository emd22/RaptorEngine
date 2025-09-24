#pragma once

#include <Core/FxDefines.hpp>
#include <Math/FxVec4.hpp>

#ifdef FX_USE_NEON
#include <arm_neon.h>

#endif

class FxQuat
{
public:
    FxQuat(float32 x, float32 y, float32 z, float32 w);

#ifdef FX_USE_NEON
    explicit FxQuat(float32x4_t intrin) : mIntrin(intrin) {}

    FxQuat& operator=(const float32x4_t& other)
    {
        mIntrin = other;
        return *this;
    }

    operator float32x4_t() const { return mIntrin; }

#endif
public:
#if defined(FX_USE_NEON)
    union alignas(16)
    {
        float32x4_t mIntrin;
        float32 mData[4];
        struct
        {
            float32 X, Y, Z, W;
        };
    };
#endif
};
