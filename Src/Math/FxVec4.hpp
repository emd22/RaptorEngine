#pragma once

#include <Core/FxTypes.hpp>
#include <format>


#ifdef FX_USE_NEON
#include "FxNeonUtil.hpp"

#include <arm_neon.h>
#elif defined(FX_USE_AVX)
#include "FxSSE.hpp"
#include "FxSSEUtil.hpp"
#endif

class alignas(16) FxVec4f
{
private:
#if defined(FX_USE_NEON)
    using SimdType = float32x4_t;
#elif defined(FX_USE_AVX)
    using SimdType = __m128;
#endif

public:
    static const FxVec4f sZero;
    static const FxVec4f sOne;

public:
    friend class FxMat4f;

    FxVec4f() = default;
    FxVec4f(float32 x, float32 y, float32 z, float32 w);
    FxVec4f(const float32* values);
    explicit FxVec4f(float32 scalar);

    FX_FORCE_INLINE void Set(float32 x, float32 y, float32 z, float32 w);


    FX_FORCE_INLINE bool IsZero() const;
    FX_FORCE_INLINE bool IsNearZero(const float32 tolerance = 0.00001) const;
    FX_FORCE_INLINE bool IsCloseTo(const FxVec4f& other, const float32 tolerance = 0.00001) const;
#ifdef FX_USE_SIMD
    FX_FORCE_INLINE bool IsCloseTo(const SimdType other, const float32 tolerance = 0.00001) const;
#endif


    FxVec4f& operator=(const FxVec4f& other);

    FX_FORCE_INLINE FxVec4f Normalize() const;
    FX_FORCE_INLINE FxVec4f& NormalizeIP();

    FX_FORCE_INLINE float32 LengthSquared() const;
    FX_FORCE_INLINE bool IsNormalized(float32 tolerance) const { return abs(LengthSquared() - 1.0f) <= tolerance; }


#ifdef FX_USE_NEON
    FX_FORCE_INLINE float32 GetX() const { return vgetq_lane_f32(mIntrin, 0); }
    FX_FORCE_INLINE float32 GetY() const { return vgetq_lane_f32(mIntrin, 1); }
    FX_FORCE_INLINE float32 GetZ() const { return vgetq_lane_f32(mIntrin, 2); }
    FX_FORCE_INLINE float32 GetW() const { return vgetq_lane_f32(mIntrin, 3); }
#else
    FX_FORCE_INLINE float32 GetX() const { return X; }
    FX_FORCE_INLINE float32 GetY() const { return Y; }
    FX_FORCE_INLINE float32 GetZ() const { return Z; }
    FX_FORCE_INLINE float32 GetW() const { return W; }
#endif

#ifdef FX_USE_NEON
    template <int TX, int TY, int TZ, int TW>
    FX_FORCE_INLINE static FxVec4f FlipSigns(const FxVec4f& vec)
    {
        return FxVec4f(FxNeon::FlipSigns<TX, TY, TZ, TW>(vec.mIntrin));
    }

#elif FX_USE_AVX
    template <int TX, int TY, int TZ, int TW>
    FX_FORCE_INLINE static FxVec4f FlipSigns(const FxVec4f& vec)
    {
        return FxVec4f(FxSSE::SetSigns<TX, TY, TZ, TW>(vec.mIntrin));
    }
#else
    template <int TX, int TY, int TZ, int TW>
    FX_FORCE_INLINE static Fxvec4f FlipSigns(const FxVec4f& vec)
    {
        constexpr float rx = TX > 0.0 ? vec.X : -vec.X;
        constexpr float ry = TY > 0.0 ? vec.Y : -vec.Y;
        constexpr float rz = TZ > 0.0 ? vec.Z : -vec.Z;
        constexpr float rw = TW > 0.0 ? vec.W : -vec.W;

        return FxVec4f(rx, ry, rz, rw);
    }
#endif

    /////////////////////////////////////
    // Operator overloads
    /////////////////////////////////////

    FX_FORCE_INLINE FxVec4f operator+(const FxVec4f& other) const;
    FX_FORCE_INLINE FxVec4f operator-(const FxVec4f& other) const;
    FX_FORCE_INLINE FxVec4f operator*(const FxVec4f& other) const;
    FX_FORCE_INLINE FxVec4f operator/(const FxVec4f& other) const;

    FX_FORCE_INLINE FxVec4f operator*(float32 scalar) const;
    FX_FORCE_INLINE FxVec4f operator/(float32 scalar) const;

    FxVec4f operator-() const;

    FxVec4f& operator+=(const FxVec4f& other);
    FxVec4f& operator-=(const FxVec4f& other);
    FxVec4f& operator*=(const FxVec4f& other);
    FX_FORCE_INLINE FxVec4f& operator*=(float32 scalar);

#ifdef FX_USE_NEON
    explicit FxVec4f(float32x4_t intrin) : mIntrin(intrin) {}

    FxVec4f& operator=(const float32x4_t& other)
    {
        mIntrin = other;
        return *this;
    }

    operator float32x4_t() const { return mIntrin; }
#elif FX_USE_AVX
    explicit FxVec4f(__m128 intrin) : mIntrin(intrin) {}

    FxVec4f& operator=(const __m128 other)
    {
        mIntrin = other;
        return *this;
    }

    explicit operator __m128() const { return mIntrin; }
#endif

public:
#if FX_USE_NEON
    union alignas(16)
    {
        float32x4_t mIntrin;
        float32 mData[4];
        struct
        {
            float32 X, Y, Z, W;
        };
    };
#elif FX_USE_AVX
    union alignas(16)
    {
        __m128 mIntrin;
        float32 mData[4];
        struct
        {
            float32 X, Y, Z, W;
        };
    };
#else
    float32 X, Y, Z, W;
#endif
};


template <>
struct std::formatter<FxVec4f>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(const FxVec4f& obj, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "({:.04}, {:.04}, {:.04}, {:.04})", obj.X, obj.Y, obj.Z, obj.W);
    }
};

/* Definitions for inline functions */
#include "Vector/FxVec4_None.inl"
#include "Vector/Simd/FxVec4_AVX.inl"
#include "Vector/Simd/FxVec4_Neon.inl"
