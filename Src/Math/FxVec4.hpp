#pragma once

#include <Core/FxTypes.hpp>

#ifdef FX_USE_NEON
#include <arm_neon.h>
#endif

class alignas(16) FxVec4f
{
public:
    static const FxVec4f Zero;
    static const FxVec4f One;

public:
    friend class FxMat4f;

    FxVec4f() = default;
    FxVec4f(float32 x, float32 y, float32 z, float32 w);
    FxVec4f(float32 values[4]);

    explicit FxVec4f(float32 scalar);

    void Set(float32 x, float32 y, float32 z, float32 w);

    FxVec4f operator+(const FxVec4f& other) const;
    FxVec4f operator-(const FxVec4f& other) const;
    FxVec4f operator*(const FxVec4f& other) const;
    FxVec4f operator/(const FxVec4f& other) const;

    FxVec4f operator*(float32 scalar) const;
    FxVec4f operator/(float32 scalar) const;

    FxVec4f operator-() const;

    FxVec4f& operator+=(const FxVec4f& other);
    FxVec4f& operator-=(const FxVec4f& other);
    FxVec4f& operator*=(const FxVec4f& other);

    FxVec4f& operator=(const FxVec4f& other);

    /**
     * Loads 4 values into the vector.
     */
    void Load4(float32 x, float32 y, float32 z, float32 w);

    /**
     * Loads 4 values into the vector from a pointer.
     */
    void Load4Ptr(float32* values);

    /**
     * Loads a single value into the vector to all components.
     */
    void Load1(float32 scalar);

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

#else
    float32 X, Y, Z, W;
#endif
};


template <>
struct std::formatter<FxVec4f>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    constexpr auto format(const FxVec4f& obj, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "({:.04}, {:.04}, {:.04}, {:.04})", obj.X, obj.Y, obj.Z, obj.W);
    }
};
