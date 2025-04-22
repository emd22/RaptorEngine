#ifdef AR_NO_SIMD

#include <Core/Defines.hpp>
#include <Math/Vec4.hpp>

inline Vec4f::Vec4f(float32 x, float32 y, float32 z, float32 w)
    : mX(x), mY(y), mZ(z), mW(w)
{
}

inline Vec4f::Vec4f(float32 scalar)
    : mX(scalar), mY(scalar), mZ(scalar), mW(scalar)
{
}

inline Vec4f Vec4f::operator + (const Vec4f &other) const
{
    Vec4f result;

    result.mX = mX + other.mX;
    result.mY = mY + other.mY;
    result.mZ = mZ + other.mZ;
    result.mW = mW + other.mW;

    return result;
}

inline Vec4f Vec4f::operator * (const Vec4f &other) const
{
    Vec4f result;

    result.mX = mX * other.mX;
    result.mY = mY * other.mY;
    result.mZ = mZ * other.mZ;
    result.mW = mW * other.mW;

    return result;
}

inline Vec4f &Vec4f::operator += (const Vec4f &other)
{
    mX += other.mX;
    mY += other.mY;
    mZ += other.mZ;
    mW += other.mW;

    return *this;
}

inline Vec4f &Vec4f::operator -= (const Vec4f &other)
{
    mX -= other.mX;
    mY -= other.mY;
    mZ -= other.mZ;
    mW -= other.mW;

    return *this;
}

inline Vec4f &Vec4f::operator *= (const Vec4f &other)
{
    mX *= other.mX;
    mY *= other.mY;
    mZ *= other.mZ;
    mW *= other.mW;

    return *this;
}

inline Vec4f Vec4f::MulAdd(const Vec4f &add_value, const Vec4f &mul_a, const Vec4f &mul_b)
{
    Vec4f result;
    result = add_value + (mul_a * mul_b);
    return result;
}

inline float32 Vec4f::GetX() const
{
    return mX;
}

inline float32 Vec4f::GetY() const
{
    return mY;
}

inline float32 Vec4f::GetZ() const
{
    return mZ;
}

inline float32 Vec4f::GetW() const
{
    return mW;
}

#endif // #ifdef AR_NO_SIMD
