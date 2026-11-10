#include <Core/Defines.hpp>

#ifdef FX_NO_SIMD

#include <Math/FVec4.hpp>

FVec4::FVec4(float32 x, float32 y, float32 z, float32 w) : X(x), Y(y), Z(z), W(w) {}

inline FVec4::FVec4(float32 scalar) : X(scalar), Y(scalar), Z(scalar), W(scalar) {}

inline FVec4 FVec4::operator+(const FVec4 other) const
{
	FVec4 result;

	result.X = X + other.X;
	result.Y = Y + other.Y;
	result.Z = Z + other.Z;
	result.W = W + other.W;

	return result;
}

inline FVec4 FVec4::operator*(const FVec4 other) const
{
	FVec4 result;

	result.X = X * other.X;
	result.Y = Y * other.Y;
	result.Z = Z * other.Z;
	result.W = W * other.W;

	return result;
}

inline FVec4& FVec4::operator+=(const FVec4 other)
{
	X += other.X;
	Y += other.Y;
	Z += other.Z;
	W += other.W;

	return *this;
}

inline FVec4& FVec4::operator-=(const FVec4 other)
{
	X -= other.X;
	Y -= other.Y;
	Z -= other.Z;
	W -= other.W;

	return *this;
}

inline FVec4& FVec4::operator*=(const FVec4 other)
{
	X *= other.X;
	Y *= other.Y;
	Z *= other.Z;
	W *= other.W;

	return *this;
}

FVec4& FVec4::operator=(const FVec4 other)
{
	X = other.X;
	Y = other.Y;
	Z = other.Z;
	W = other.W;

	return *this;
}

inline FVec4 FVec4::MulAdd(const FVec4 add_value, const FVec4 mul_a, const FVec4 mul_b)
{
	FVec4 result;
	result = add_value + (mul_a * mul_b);
	return result;
}

inline float32 FVec4::GetX() const { return X; }

inline float32 FVec4::GetY() const { return Y; }

inline float32 FVec4::GetZ() const { return Z; }

inline float32 FVec4::GetW() const { return W; }

inline void FVec4::Load1(float32 scalar) noexcept { X = Y = Z = W = scalar; }

#endif // #ifdef FX_NO_SIMD
