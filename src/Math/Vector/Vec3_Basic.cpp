#ifdef AR_NO_SIMD

#include <Core/Defines.hpp>
#include <Math/Vec3.hpp>

inline Vec3f::Vec3f(float32 x, float32 y, float32 z)
    : mX(x), mY(y), mZ(z)
{
}

inline Vec3f::Vec3f(float32 scalar)
    : mX(scalar), mY(scalar), mZ(scalar)
{
}

inline Vec3f Vec3f::operator + (const Vec3f &other) const
{
    Vec3f result;

    result.mX = mX + other.mX;
    result.mY = mY + other.mY;
    result.mZ = mZ + other.mZ;

    return result;
}

inline Vec3f Vec3f::operator * (const Vec3f &other) const
{
    Vec3f result;

    result.mX = mX * other.mX;
    result.mY = mY * other.mY;
    result.mZ = mZ * other.mZ;

    return result;
}

inline Vec3f &Vec3f::operator += (const Vec3f &other)
{
    mX += other.mX;
    mY += other.mY;
    mZ += other.mZ;

    return *this;
}

inline Vec3f &Vec3f::operator -= (const Vec3f &other)
{
    mX -= other.mX;
    mY -= other.mY;
    mZ -= other.mZ;

    return *this;
}

inline Vec3f &Vec3f::operator *= (const Vec3f &other)
{
    mX *= other.mX;
    mY *= other.mY;
    mZ *= other.mZ;

    return *this;
}

inline Vec3f Vec3f::MulAdd(const Vec3f &add_value, const Vec3f &mul_a, const Vec3f &mul_b)
{
    Vec3f result;
    result = add_value + (mul_a * mul_b);
    return result;
}

inline float32 Vec3f::GetX() const
{
    return mX;
}

inline float32 Vec3f::GetY() const
{
    return mY;
}

inline float32 Vec3f::GetZ() const
{
    return mZ;
}

#endif // #ifdef AR_NO_SIMD
