#pragma once

#include <Core/FxTypes.hpp>

#ifdef FX_USE_NEON
#include <arm_neon.h>
#endif

namespace JPH {
class Vec3;
using RVec3 = Vec3;
} // namespace JPH

class alignas(16) FxVec3f
{
public:
    static const FxVec3f Zero;
    static const FxVec3f One;
    static const FxVec3f Up;

public:
    FxVec3f() = default;
    FxVec3f(float32 x, float32 y, float32 z);
    FxVec3f(float32* values);

    explicit FxVec3f(float32 scalar);

    void Set(float32 x, float32 y, float32 z);

    void ToJoltVec3(JPH::RVec3& jolt_vec);

    FxVec3f operator+(const FxVec3f& other) const;
    FxVec3f operator-(const FxVec3f& other) const;
    FxVec3f operator*(const FxVec3f& other) const;

    FxVec3f operator*(float32 scalar) const;
    FxVec3f operator/(float32 scalar) const;

    FxVec3f operator-() const;

    FxVec3f& operator+=(const FxVec3f& other);
    FxVec3f& operator-=(const FxVec3f& other);
    FxVec3f& operator*=(const FxVec3f& other);

    static FxVec3f MulAdd(const FxVec3f& add_value, const FxVec3f& mul_a, const FxVec3f& mul_b);

    FX_FORCE_INLINE FxVec3f Normalize() const;
    FX_FORCE_INLINE void NormalizeIP();

    FX_FORCE_INLINE float32 Length() const;
    FxVec3f Cross(const FxVec3f& other) const;
    FxVec3f CrossSlow(const FxVec3f& other) const;

    float32 Dot(const FxVec3f& other) const;

    static FxVec3f Min(const FxVec3f& a, const FxVec3f& b);
    static FxVec3f Max(const FxVec3f& a, const FxVec3f& b);

    void Print() const;

#ifdef FX_USE_NEON
    explicit FxVec3f(float32x4_t intrin) : mIntrin(intrin) {}

    FxVec3f& operator=(const float32x4_t& other)
    {
        mIntrin = other;
        return *this;
    }

    FxVec3f& operator=(const float32x4_t other)
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
        float mData[4];

        struct
        {
            float32 X, Y, Z, mPadding0;
        };
    };

#else
    struct
    {
        float32 X, Y, Z;
    };
#endif
};

template <>
struct std::formatter<FxVec3f>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    constexpr auto format(const FxVec3f& obj, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "({:.04}, {:.04}, {:.04})", obj.X, obj.Y, obj.Z);
    }
};
