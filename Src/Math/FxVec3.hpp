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
    static const FxVec3f sZero;
    static const FxVec3f sOne;

    static const FxVec3f sUp;
    static const FxVec3f sRight;
    static const FxVec3f sForward;

public:
    FxVec3f() = default;
    FxVec3f(float32 x, float32 y, float32 z);
    FxVec3f(float32* values);
    FxVec3f(const JPH::Vec3& other);

    explicit FxVec3f(float32 scalar);

    FX_FORCE_INLINE void Set(float32 x, float32 y, float32 z);

    void ToJoltVec3(JPH::RVec3& jolt_vec) const;
    void FromJoltVec3(const JPH::RVec3& jolt_vec);

    FX_FORCE_INLINE FxVec3f operator+(const FxVec3f& other) const;
    FX_FORCE_INLINE FxVec3f operator-(const FxVec3f& other) const;
    FX_FORCE_INLINE FxVec3f operator*(const FxVec3f& other) const;

    FX_FORCE_INLINE FxVec3f operator*(float32 scalar) const;
    FX_FORCE_INLINE FxVec3f operator/(float32 scalar) const;

    FX_FORCE_INLINE FxVec3f operator-() const;

    FX_FORCE_INLINE FxVec3f& operator+=(const FxVec3f& other);
    FX_FORCE_INLINE FxVec3f& operator-=(const FxVec3f& other);
    FX_FORCE_INLINE FxVec3f& operator*=(const FxVec3f& other);

    FX_FORCE_INLINE bool operator==(const FxVec3f& other) const;
    bool operator==(const JPH::Vec3& other) const;

    FX_FORCE_INLINE bool IsCloseTo(const FxVec3f& other, const float32 tolerance = 0.00001) const;
    bool IsCloseTo(const JPH::Vec3& other, const float32 threshold = 0.001) const;

    FxVec3f Normalize() const;
    void NormalizeIP();

    float32 Length() const;
    FxVec3f Cross(const FxVec3f& other) const;
    FxVec3f CrossSlow(const FxVec3f& other) const;

    float32 Dot(const FxVec3f& other) const;

    FX_FORCE_INLINE static FxVec3f Min(const FxVec3f& a, const FxVec3f& b);
    FX_FORCE_INLINE static FxVec3f Max(const FxVec3f& a, const FxVec3f& b);

    void Print() const;

#ifdef FX_USE_NEON
    FX_FORCE_INLINE float32 GetX() const { return vgetq_lane_f32(mIntrin, 0); }
    FX_FORCE_INLINE float32 GetY() const { return vgetq_lane_f32(mIntrin, 1); }
    FX_FORCE_INLINE float32 GetZ() const { return vgetq_lane_f32(mIntrin, 2); }

    FX_FORCE_INLINE bool IsCloseTo(const float32x4_t& other, const float32 tolerance = 0.00001) const;

#else
    FX_FORCE_INLINE float32 GetX() const { return X; }
    FX_FORCE_INLINE float32 GetY() const { return Y; }
    FX_FORCE_INLINE float32 GetZ() const { return Z; }

#endif

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
        float32 mData[4];

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


#include "Vector/FxVec3_None.inl"
#include "Vector/Simd/FxVec3_Neon.inl"
