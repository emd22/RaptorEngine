#pragma once

#include <Core/Types.hpp>

#ifdef FX_USE_NEON
#include <arm_neon.h>
#endif

class Vec3f {
public:
    Vec3f() = default;
    Vec3f(float32 x, float32 y, float32 z);
    explicit Vec3f(float32 scalar);

    Vec3f operator + (const Vec3f &other) const;
    Vec3f operator * (const Vec3f &other) const;

    Vec3f &operator += (const Vec3f &other);
    Vec3f &operator -= (const Vec3f &other);
    Vec3f &operator *= (const Vec3f &other);

    static Vec3f MulAdd(const Vec3f &add_value, const Vec3f &mul_a, const Vec3f &mul_b);

    float32 GetX() const;
    float32 GetY() const;
    float32 GetZ() const;

    static const Vec3f Zero()
    {
        return Vec3f(0.0f, 0.0f, 0.0f);
    };

#ifdef FX_USE_NEON
    explicit Vec3f(float32x4_t intrin)
        : mIntrin(intrin)
    {
    }

    operator float32x4_t() const
    {
        return mIntrin;
    }
#endif

private:
#if defined(FX_USE_NEON)
    union alignas(16) {
        float32x4_t mIntrin;
        float mData[4];
    };

#else
    float32 mX, mY, mZ;
#endif
};
