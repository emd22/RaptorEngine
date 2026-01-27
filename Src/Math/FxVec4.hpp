#pragma once

#include <Core/FxTypes.hpp>

#ifdef FX_USE_NEON
#include <arm_neon.h>
#elif defined(FX_USE_AVX)
#include "FxSSE.hpp"
#endif

class alignas(16) FxVec4f
{
public:
    static const FxVec4f sZero;
    static const FxVec4f sOne;

public:
    friend class FxMat4f;

    FxVec4f() = default;
    FxVec4f(float32 x, float32 y, float32 z, float32 w);
    FxVec4f(float32 values[4]);

    explicit FxVec4f(float32 scalar);

    void Set(float32 x, float32 y, float32 z, float32 w);

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

    FxVec4f& operator=(const FxVec4f& other);

    /**
     * Loads 4 values into the vector.
     */
    void Load4(float32 x, float32 y, float32 z, float32 w);

    /**
     * Loads 4 values into the vector from a pointer.
     */
    void Load4Ptr(const float32* values);

    /**
     * Loads a single value into the vector to all components.
     */
    void Load1(float32 scalar);

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
