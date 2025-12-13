#pragma once

#include <Core/FxDefines.hpp>
#include <Math/FxVec3.hpp>
#include <Math/FxVec4.hpp>

#ifdef FX_USE_NEON
#include <arm_neon.h>
#elif FX_USE_AVX
#include <immintrin.h>
#endif

namespace JPH {
class Quat;
}

class FxQuat
{
public:
    static const FxQuat sIdentity;

public:
    FxQuat() = default;
    FxQuat(float32 x, float32 y, float32 z, float32 w);
    FxQuat(const JPH::Quat& other);

    static FxQuat FromAxisAngle(FxVec3f axis, float32 angle);

    static FxQuat FromEulerAngles(FxVec3f angles);
    // static FxQuat FromEulerAngles_NeonTest(FxVec3f angles);

    FxVec3f GetEulerAngles() const;

    FxQuat operator*(const FxQuat& other) const;

    void FromJoltQuaternion(const JPH::Quat& quat);
    void ToJoltQuaternion(JPH::Quat& quat) const;

    FX_FORCE_INLINE bool IsCloseTo(const FxQuat& other, const float32 tolerance = 0.0001) const;
    bool IsCloseTo(const JPH::Quat& other, const float32 tolerance = 0.0001) const;

#ifdef FX_USE_NEON
    FX_FORCE_INLINE float32 GetX() const { return vgetq_lane_f32(mIntrin, 0); }
    FX_FORCE_INLINE float32 GetY() const { return vgetq_lane_f32(mIntrin, 1); }
    FX_FORCE_INLINE float32 GetZ() const { return vgetq_lane_f32(mIntrin, 2); }
    FX_FORCE_INLINE float32 GetW() const { return vgetq_lane_f32(mIntrin, 3); }

    FX_FORCE_INLINE bool IsCloseTo(const float32x4_t& other, const float32 tolerance = 0.0001) const;
#elif defined(FX_USE_AVX)
    FX_FORCE_INLINE float32 GetX() const { return X; }
    FX_FORCE_INLINE float32 GetY() const { return Y; }
    FX_FORCE_INLINE float32 GetZ() const { return Z; }
    FX_FORCE_INLINE float32 GetW() const { return W; }

    FX_FORCE_INLINE bool IsCloseTo(const __m128 other, const float32 tolerance = 0.0001) const;
#else
    FX_FORCE_INLINE float32 GetX() const { return X; }
    FX_FORCE_INLINE float32 GetY() const { return Y; }
    FX_FORCE_INLINE float32 GetZ() const { return Z; }
    FX_FORCE_INLINE float32 GetW() const { return W; }
#endif

#ifdef FX_USE_NEON
    explicit FxQuat(float32x4_t intrin) : mIntrin(intrin) {}

    FX_FORCE_INLINE FxQuat& operator=(const float32x4_t& other);

    operator float32x4_t() const { return mIntrin; }
#elif defined(FX_USE_AVX)
    explicit FxQuat(__m128 intrin) : mIntrin(intrin) {}

    FX_FORCE_INLINE FxQuat &operator=(const __m128 &other);

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
#elif defined(FX_USE_AVX)
    union alignas(16)
    {
        __m128 mIntrin;
        float32 mData[4];
        struct {
            float32 X, Y, Z, W;
        };
    };
#endif
};


template <>
struct std::formatter<FxQuat>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(const FxQuat& obj, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "({:.04}, {:.04}, {:.04}, {:.04})", obj.X, obj.Y, obj.Z, obj.W);
    }
};

#include "Quaternion/FxQuat_Neon.inl"
#include "Quaternion/FxQuat_AVX.inl"
