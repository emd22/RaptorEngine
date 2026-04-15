#pragma once

#include <Core/Defines.hpp>
#include <Core/Types.hpp>
#include <format>

#ifdef FX_USE_NEON
#include "NeonUtil.hpp"

#include <arm_neon.h>
#elif defined(FX_USE_AVX)
#include "SSE.hpp"
#include "SSEUtil.hpp"

#endif

namespace JPH {
class Vec3;
using RVec3 = Vec3;
} // namespace JPH


namespace fx {

class Vec4f;
class Quat;

class alignas(16) Vec3f
{
private:
#if defined(FX_USE_NEON)
    using SimdType = float32x4_t;
#elif defined(FX_USE_AVX)
    using SimdType = __m128;
#endif

public:
    static const Vec3f sZero;
    static const Vec3f sOne;

    static const Vec3f sUp;
    static const Vec3f sRight;
    static const Vec3f sForward;

public:
    Vec3f() = default;
    FX_FORCE_INLINE Vec3f(float32 x, float32 y, float32 z) : Vec3f(x, y, z, 0.0f) {}
    FX_FORCE_INLINE Vec3f(float32 x, float32 y, float32 z, float32 w);
    FX_FORCE_INLINE Vec3f(const float32* values);
    Vec3f(const JPH::Vec3& other);

    FX_FORCE_INLINE explicit Vec3f(float32 scalar);

    FX_FORCE_INLINE static Vec3f MulAdd(const Vec3f& a, const Vec3f& b, const Vec3f& accum);

#ifdef FX_USE_SIMD
    explicit Vec3f(SimdType intrin) : mIntrin(intrin) {}
    Vec3f(const Vec4f& other);
#else
    Vec3f(const Vec4f& other) : X(other.X), Y(other.Y), Z(other.Z) {}
#endif


    FX_FORCE_INLINE void Set(float32 x, float32 y, float32 z);

    void ToJoltVec3(JPH::RVec3& jolt_vec) const;
    void FromJoltVec3(const JPH::RVec3& jolt_vec);

    static FX_FORCE_INLINE Vec3f FromDifference(const float32* a, const float32* b)
    {
        const float32 vx = (a[0] - b[0]);
        const float32 vy = (a[1] - b[1]);
        const float32 vz = (a[2] - b[2]);

        return Vec3f(vx, vy, vz);
    }

    FX_FORCE_INLINE void CopyTo(float32* output)
    {
        output[0] = X;
        output[1] = Y;
        output[2] = Z;
    }

    FX_FORCE_INLINE float32 DistanceTo(const Vec3f& other) const { return (other - *this).Length(); }

    FX_FORCE_INLINE bool IntersectsSphere(const Vec3f& sphere_center, float32 sphere_radius) const
    {
        const Vec3f diff = (*this) - sphere_center;
        const float dist2 = diff.Dot(diff);

        return dist2 <= sphere_radius * sphere_radius;
    }

    FX_FORCE_INLINE bool operator==(const Vec3f& other) const;
    bool operator==(const JPH::Vec3& other) const;

    FX_FORCE_INLINE bool IsZero() const;
    FX_FORCE_INLINE bool IsNearZero(const float32 tolerance = 0.00001) const;
    FX_FORCE_INLINE bool IsCloseTo(const Vec3f& other, const float32 tolerance = 0.00001) const;
#ifdef FX_USE_SIMD
    FX_FORCE_INLINE bool IsCloseTo(const SimdType other, const float32 tolerance = 0.00001) const;
#endif
    bool IsCloseTo(const JPH::Vec3& other, const float32 threshold = 0.001) const;

    FX_FORCE_INLINE Vec3f Normalize() const;

    /**
     * Normalizes the vector in place (modifies the source vector.)
     * @returns The modified vector.
     */
    FX_FORCE_INLINE Vec3f& NormalizeIP();

    FX_FORCE_INLINE float32 Length() const;
    Vec3f Cross(const Vec3f& other) const;
    Vec3f CrossSlow(const Vec3f& other) const;

    Vec3f Rotate(const Quat& rotation) const;

    FX_FORCE_INLINE float32 Dot(const Vec3f& other) const;
#ifdef FX_USE_SIMD
    FX_FORCE_INLINE float32 Dot(SimdType other) const;
#endif

    FX_FORCE_INLINE static Vec3f Min(const Vec3f& a, const Vec3f& b);
    FX_FORCE_INLINE static Vec3f Max(const Vec3f& a, const Vec3f& b);
    FX_FORCE_INLINE static Vec3f Clamp(const Vec3f& v, const Vec3f& min, const Vec3f& max);
    FX_FORCE_INLINE static Vec3f Lerp(const Vec3f& a, const Vec3f& b, const float f);
    FX_FORCE_INLINE Vec3f& LerpIP(const Vec3f& dest, const float step);

    FX_FORCE_INLINE Vec3f& SmoothInterpolate(const Vec3f& dest, const float speed, const float delta_time)
    {
        LerpIP(dest, 1.0f - expf(-speed * delta_time));
        return *this;
    }

    void Print() const;

#ifdef FX_USE_NEON
    // Neon has optimized fetch's when using the intrinsic
    FX_FORCE_INLINE float32 GetX() const { return vgetq_lane_f32(mIntrin, 0); }
    FX_FORCE_INLINE float32 GetY() const { return vgetq_lane_f32(mIntrin, 1); }
    FX_FORCE_INLINE float32 GetZ() const { return vgetq_lane_f32(mIntrin, 2); }
#else
    FX_FORCE_INLINE float32 GetX() const { return X; }
    FX_FORCE_INLINE float32 GetY() const { return Y; }
    FX_FORCE_INLINE float32 GetZ() const { return Z; }
#endif

#ifdef FX_USE_NEON
    template <int TX, int TY, int TZ, int TW>
    FX_FORCE_INLINE static Vec3f FlipSigns(const Vec3f& vec)
    {
        return Vec3f(Neon::FlipSigns<TX, TY, TZ, TW>(vec.mIntrin));
    }

#elif FX_USE_AVX
    template <int TX, int TY, int TZ, int TW>
    FX_FORCE_INLINE static Vec3f FlipSigns(const Vec3f& vec)
    {
        return Vec3f(SSE::FlipSigns<TX, TY, TZ, TW>(vec.mIntrin));
    }
#else
    template <int TX, int TY, int TZ, int TW>
    FX_FORCE_INLINE static Vec3f FlipSigns(const Vec3f& vec)
    {
        constexpr float rx = TX > 0.0 ? vec.X : -vec.X;
        constexpr float ry = TY > 0.0 ? vec.Y : -vec.Y;
        constexpr float rz = TZ > 0.0 ? vec.Z : -vec.Z;
        constexpr float rw = TW > 0.0 ? vec.W : -vec.W;

        return Vec3f(rx, ry, rz, rw);
    }
#endif

    /////////////////////////////////////
    // Operator overloads
    /////////////////////////////////////

    FX_FORCE_INLINE Vec3f operator+(const Vec3f& other) const;
    FX_FORCE_INLINE Vec3f operator-(const Vec3f& other) const;
    FX_FORCE_INLINE Vec3f operator*(const Vec3f& other) const;
    FX_FORCE_INLINE Vec3f operator/(const Vec3f& other) const;

    FX_FORCE_INLINE Vec3f operator*(float32 scalar) const;
    FX_FORCE_INLINE Vec3f operator/(float32 scalar) const;

    FX_FORCE_INLINE Vec3f operator-() const;

    FX_FORCE_INLINE Vec3f& operator+=(const Vec3f& other);
    FX_FORCE_INLINE Vec3f& operator-=(float32 scalar);
    FX_FORCE_INLINE Vec3f& operator-=(const Vec3f& other);
    FX_FORCE_INLINE Vec3f& operator*=(const Vec3f& other);

#ifdef FX_USE_SIMD
    Vec3f& operator=(const SimdType& other)
    {
        mIntrin = other;
        return *this;
    }

    Vec3f& operator=(const SimdType other)
    {
        mIntrin = other;
        return *this;
    }

    operator SimdType() const { return mIntrin; }
#endif

public:
#if defined(FX_USE_SIMD)
    union alignas(16)
    {
        SimdType mIntrin;
        float32 mData[4];

        struct
        {
            float32 X, Y, Z, W;
        };
    };
#else
    struct
    {
        float32 X, Y, Z;
    };
#endif
};

} // namespace fx


template <>
struct std::formatter<fx::Vec3f>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(const fx::Vec3f& obj, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "({:.04f}, {:.04f}, {:.04f}, {:.04f})", obj.X, obj.Y, obj.Z, obj.W);
    }
};


#include "Impl/Vector/Vec3_AVX.inl"
#include "Impl/Vector/Vec3_Fallback.inl"
#include "Impl/Vector/Vec3_Neon.inl"
