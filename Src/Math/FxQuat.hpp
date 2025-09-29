#pragma once

#include <Core/FxDefines.hpp>
#include <Math/FxVec3.hpp>
#include <Math/FxVec4.hpp>

#ifdef FX_USE_NEON
#include <arm_neon.h>

#endif

class FxQuat
{
public:
    FxQuat() = default;
    FxQuat(float32 x, float32 y, float32 z, float32 w);

    static FxQuat FromEulerAngles(FxVec3f angles);
    static FxQuat FromEulerAngles_NeonTest(FxVec3f angles);

    FxVec3f GetEulerAngles() const;

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


template <>
struct std::formatter<FxQuat>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    constexpr auto format(const FxQuat& obj, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "({:.04}, {:.04}, {:.04}, {:.04})", obj.X, obj.Y, obj.Z, obj.W);
    }
};
