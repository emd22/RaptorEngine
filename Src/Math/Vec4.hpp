#pragma once

#include <Core/Types.hpp>
#include <format>


#ifdef FX_USE_NEON
#include "NeonUtil.hpp"

#include <arm_neon.h>
#elif defined(FX_USE_AVX)
#include "SSE.hpp"
#include "SSEUtil.hpp"
#endif

namespace fx {

class alignas(16) Vec4f
{
private:
#if defined(FX_USE_NEON)
    using SimdType = float32x4_t;
#elif defined(FX_USE_AVX)
    using SimdType = __m128;
#endif

public:
    static const Vec4f sZero;
    static const Vec4f sOne;

public:
    friend class Mat4f;

    Vec4f() = default;
    Vec4f(float32 x, float32 y, float32 z, float32 w);
    Vec4f(const float32* values);
    explicit Vec4f(float32 scalar);

    FX_FORCE_INLINE void Set(float32 x, float32 y, float32 z, float32 w);
    FX_FORCE_INLINE void Set(float32 scalar);


    FX_FORCE_INLINE bool IsZero() const;
    FX_FORCE_INLINE bool IsNearZero(const float32 tolerance = 0.00001) const;
    FX_FORCE_INLINE bool IsCloseTo(const Vec4f& other, const float32 tolerance = 0.00001) const;
#ifdef FX_USE_SIMD
    FX_FORCE_INLINE bool IsCloseTo(const SimdType other, const float32 tolerance = 0.00001) const;
#endif

    Vec4f& operator=(const Vec4f& other);

    FX_FORCE_INLINE Vec4f Normalize() const;
    FX_FORCE_INLINE Vec4f& NormalizeIP();

    FX_FORCE_INLINE float32 LengthSquared() const;
    FX_FORCE_INLINE bool IsNormalized(float32 tolerance) const { return abs(LengthSquared() - 1.0f) <= tolerance; }


#ifdef FX_USE_NEON
#else
    FX_FORCE_INLINE float32 GetX() const { return X; }
    FX_FORCE_INLINE float32 GetY() const { return Y; }
    FX_FORCE_INLINE float32 GetZ() const { return Z; }
    FX_FORCE_INLINE float32 GetW() const { return W; }
#endif

#ifdef FX_USE_NEON
    template <int TX, int TY, int TZ, int TW>
    FX_FORCE_INLINE static Vec4f FlipSigns(const Vec4f& vec)
    {
        return Vec4f(Neon::FlipSigns<TX, TY, TZ, TW>(vec.mIntrin));
    }

#elif FX_USE_AVX
    template <int TX, int TY, int TZ, int TW>
    FX_FORCE_INLINE static Vec4f FlipSigns(const Vec4f& vec)
    {
        return Vec4f(SSE::SetSigns<TX, TY, TZ, TW>(vec.mIntrin));
    }
#else
    template <int TX, int TY, int TZ, int TW>
    FX_FORCE_INLINE static vec4f FlipSigns(const Vec4f& vec)
    {
        constexpr float rx = TX > 0.0 ? vec.X : -vec.X;
        constexpr float ry = TY > 0.0 ? vec.Y : -vec.Y;
        constexpr float rz = TZ > 0.0 ? vec.Z : -vec.Z;
        constexpr float rw = TW > 0.0 ? vec.W : -vec.W;

        return Vec4f(rx, ry, rz, rw);
    }
#endif

    /////////////////////////////////////
    // Operator overloads
    /////////////////////////////////////

    FX_FORCE_INLINE Vec4f operator+(const Vec4f& other) const;
    FX_FORCE_INLINE Vec4f operator-(const Vec4f& other) const;
    FX_FORCE_INLINE Vec4f operator*(const Vec4f& other) const;
    FX_FORCE_INLINE Vec4f operator/(const Vec4f& other) const;

    FX_FORCE_INLINE Vec4f operator*(float32 scalar) const;
    FX_FORCE_INLINE Vec4f operator/(float32 scalar) const;

    Vec4f operator-() const;

    Vec4f& operator+=(const Vec4f& other);
    Vec4f& operator-=(const Vec4f& other);
    Vec4f& operator*=(const Vec4f& other);
    FX_FORCE_INLINE Vec4f& operator*=(float32 scalar);

#ifdef FX_USE_SIMD
    explicit Vec4f(SimdType intrin) : mIntrin(intrin) {}

    Vec4f& operator=(SimdType other)
    {
        mIntrin = other;
        return *this;
    }
#endif

public:
#if FX_USE_SIMD
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
    float32 X, Y, Z, W;
#endif
};


/////////////////////////////////////
// Integer vector classes
/////////////////////////////////////

class alignas(16) Vec4u
{
private:
#if defined(FX_USE_NEON)
    using SimdType = uint32x4_t;
#elif defined(FX_USE_AVX)
    using SimdType = __m128i;
#endif

public:
    static const Vec4u sZero;
    static const Vec4u sOne;

public:
    Vec4u() = default;
    Vec4u(uint32 x, uint32 y, uint32 z, uint32 w);
    Vec4u(const uint32* values);
    explicit Vec4u(uint32 scalar);

    FX_FORCE_INLINE void Set(uint32 x, uint32 y, uint32 z, uint32 w);
    FX_FORCE_INLINE void Set(uint32 scalar);

    FX_FORCE_INLINE bool IsZero() const;

    Vec4u& operator=(const Vec4u& other);

#ifdef FX_USE_NEON
    FX_FORCE_INLINE uint32 GetX() const { return vgetq_lane_u32(mIntrin, 0); }
    FX_FORCE_INLINE uint32 GetY() const { return vgetq_lane_u32(mIntrin, 1); }
    FX_FORCE_INLINE uint32 GetZ() const { return vgetq_lane_u32(mIntrin, 2); }
    FX_FORCE_INLINE uint32 GetW() const { return vgetq_lane_u32(mIntrin, 3); }
#else
    FX_FORCE_INLINE uint32 GetX() const { return X; }
    FX_FORCE_INLINE uint32 GetY() const { return Y; }
    FX_FORCE_INLINE uint32 GetZ() const { return Z; }
    FX_FORCE_INLINE uint32 GetW() const { return W; }
#endif

#ifdef FX_USE_NEON
    template <int TX, int TY, int TZ, int TW>
    FX_FORCE_INLINE static Vec4u FlipSigns(const Vec4u& vec)
    {
        return Vec4u(Neon::FlipSigns<TX, TY, TZ, TW>(vec.mIntrin));
    }

#elif FX_USE_AVX
    template <int TX, int TY, int TZ, int TW>
    FX_FORCE_INLINE static Vec4u FlipSigns(const Vec4u& vec)
    {
        return Vec4u(SSE::SetSigns<TX, TY, TZ, TW>(vec.mIntrin));
    }
#else
    template <int TX, int TY, int TZ, int TW>
    FX_FORCE_INLINE static Vec4u FlipSigns(const Vec4u& vec)
    {
        constexpr float rx = TX > 0.0 ? vec.X : -vec.X;
        constexpr float ry = TY > 0.0 ? vec.Y : -vec.Y;
        constexpr float rz = TZ > 0.0 ? vec.Z : -vec.Z;
        constexpr float rw = TW > 0.0 ? vec.W : -vec.W;

        return Vec4u(rx, ry, rz, rw);
    }
#endif

    /////////////////////////////////////
    // Operator overloads
    /////////////////////////////////////

    FX_FORCE_INLINE Vec4u operator+(const Vec4u& other) const;
    FX_FORCE_INLINE Vec4u operator-(const Vec4u& other) const;
    FX_FORCE_INLINE Vec4u operator*(const Vec4u& other) const;

    FX_FORCE_INLINE Vec4u operator*(uint32 scalar) const;

    Vec4u& operator+=(const Vec4u& other);
    Vec4u& operator-=(const Vec4u& other);
    Vec4u& operator*=(const Vec4u& other);
    FX_FORCE_INLINE Vec4u& operator*=(uint32 scalar);

#ifdef FX_USE_SIMD
    Vec4u& operator=(const SimdType other)
    {
        mIntrin = other;
        return *this;
    }

    explicit Vec4u(SimdType intrin) : mIntrin(intrin) {}
#endif

public:
#if FX_USE_SIMD
    union alignas(16)
    {
        SimdType mIntrin;
        uint32 mData[4];
        struct
        {
            uint32 X, Y, Z, W;
        };
    };
#else
    uint32 X, Y, Z, W;
#endif
};

} // namespace fx


template <>
struct std::formatter<fx::Vec4f>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(const fx::Vec4f& obj, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "({:.04}, {:.04}, {:.04}, {:.04})", obj.X, obj.Y, obj.Z, obj.W);
    }
};
template <>
struct std::formatter<fx::Vec4u>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(const fx::Vec4u& obj, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "({}, {}, {}, {})", obj.X, obj.Y, obj.Z, obj.W);
    }
};


/* Definitions for inline functions */
#include "Impl/Vector/Vec4_AVX.inl"
#include "Impl/Vector/Vec4_Fallback.inl"
#include "Impl/Vector/Vec4_Neon.inl"
