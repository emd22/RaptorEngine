#pragma once

#include <Core/Types.hpp>

#ifdef FX_USE_NEON
#include <arm_neon.h>
#endif

class alignas(16) FxVec4f
{
public:
    friend class FxMat4f;

    FxVec4f() = default;
    FxVec4f(float32 x, float32 y, float32 z, float32 w);
    FxVec4f(float32 values[4]);

    explicit FxVec4f(float32 scalar);

    FxVec4f operator + (const FxVec4f &other) const;
    FxVec4f operator * (const FxVec4f &other) const;

    FxVec4f &operator += (const FxVec4f &other);
    FxVec4f &operator -= (const FxVec4f &other);
    FxVec4f &operator *= (const FxVec4f &other);

    FxVec4f &operator = (const FxVec4f &other);

    static FxVec4f MulAdd(const FxVec4f &add_value, const FxVec4f &mul_a, const FxVec4f &mul_b);

    float32 GetX() const;
    float32 GetY() const;
    float32 GetZ() const;
    float32 GetW() const;

    void SetX(float32 x);
    void SetY(float32 y);
    void SetZ(float32 z);
    void SetW(float32 w);

    /**
     * Loads 4 values into the vector.
     */
    void Load4(float32 x, float32 y, float32 z, float32 w);

    /**
     * Loads 4 values into the vector from a pointer.
     */
    void Load4Ptr(float32 *values);

    /**
     * Loads a single value into the vector to all components.
     */
    void Load1(float32 scalar);

    static const FxVec4f Zero()
    {
        return FxVec4f(0.0f, 0.0f, 0.0f, 0.0f);
    };

#ifdef FX_USE_NEON
    explicit FxVec4f(float32x4_t intrin)
        : mIntrin(intrin)
    {
    }

    FxVec4f &operator = (const float32x4_t &other)
    {
        mIntrin = other;
        return *this;
    }

    operator float32x4_t() const
    {
        return mIntrin;
    }
#endif

public:
#if defined(FX_USE_NEON)
    union alignas(16) {
        float32x4_t mIntrin;
        float32 mData[4];
        struct { float32 X, Y, Z, W; };
    };

#else
    float32 X, Y, Z, W;
#endif
};
