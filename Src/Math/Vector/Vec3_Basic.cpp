#ifdef FX_NO_SIMD

#include <Core/FxDefines.hpp>
#include <Math/FxVec3.hpp>

inline Vec3f::Vec3f(float32 x, float32 y, float32 z)
    : X(x), Y(y), Z(z)
{
}

inline Vec3f::Vec3f(float32 scalar)
    : X(scalar), Y(scalar), Z(scalar)
{
}

inline Vec3f Vec3f::operator + (const Vec3f &other) const
{
    Vec3f result;

    result.X = X + other.X;
    result.Y = Y + other.Y;
    result.Z = Z + other.Z;

    return result;
}

inline Vec3f Vec3f::operator * (const Vec3f &other) const
{
    Vec3f result;

    result.X = X * other.X;
    result.Y = Y * other.Y;
    result.Z = Z * other.Z;

    return result;
}

inline Vec3f &Vec3f::operator += (const Vec3f &other)
{
    X += other.X;
    Y += other.Y;
    Z += other.Z;

    return *this;
}

inline Vec3f &Vec3f::operator -= (const Vec3f &other)
{
    X -= other.X;
    Y -= other.Y;
    Z -= other.Z;

    return *this;
}

inline Vec3f &Vec3f::operator *= (const Vec3f &other)
{
    X *= other.X;
    Y *= other.Y;
    Z *= other.Z;

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
    return X;
}

inline float32 Vec3f::GetY() const
{
    return Y;
}

inline float32 Vec3f::GetZ() const
{
    return Z;
}

#endif // #ifdef FX_NO_SIMD
