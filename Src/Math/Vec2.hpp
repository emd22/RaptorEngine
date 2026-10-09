#pragma once

#include <Core/Types.hpp>
#include <cmath>
#include <concepts>
#include <format>

namespace fx {


template <typename Type>
class Vec2Base
{
public:
	static const Vec2Base<Type> sZero;

public:
	constexpr Vec2Base() = default;

	constexpr explicit Vec2Base(Type x, Type y) : X(x), Y(y) {}
	constexpr explicit Vec2Base(Type scalar) : X(scalar), Y(scalar) {}


	Vec2Base operator-() const { return Vec2Base(-X, -Y); }


	Vec2Base operator+(const Vec2Base& other) const { return Vec2Base(X + other.X, Y + other.Y); }
	Vec2Base operator-(const Vec2Base& other) const { return Vec2Base(X - other.X, Y - other.Y); }
	Vec2Base operator*(const Vec2Base& other) const { return Vec2Base(X * other.X, Y * other.Y); }
	Vec2Base operator/(const Vec2Base& other) const { return Vec2Base(X / other.X, Y / other.Y); }

	Vec2Base operator+(Type scalar) const { return Vec2Base(X + scalar, Y + scalar); }
	Vec2Base operator-(Type scalar) const { return Vec2Base(X - scalar, Y - scalar); }
	Vec2Base operator*(Type scalar) const { return Vec2Base(X * scalar, Y * scalar); }
	Vec2Base operator/(Type scalar) const { return Vec2Base(X / scalar, Y / scalar); }

	Vec2Base& operator+=(const Vec2Base& other)
	{
		X += other.X;
		Y += other.Y;

		return *this;
	}

	Vec2Base& operator-=(const Vec2Base& other)
	{
		X -= other.X;
		Y -= other.Y;

		return *this;
	}

	Vec2Base& operator*=(const Vec2Base& other)
	{
		X *= other.X;
		Y *= other.Y;

		return *this;
	}


	/**
	 * @brief Checks if a vector is **exactly** equal to another vector. This does not account for floating point error.
	 */
	bool operator==(const Vec2Base<Type>& other) const { return (X == other.X && Y == other.Y); }

	Vec2Base<Type>& operator=(const Vec2Base<Type>& other)
	{
		X = other.X;
		Y = other.Y;
		return *this;
	}

	void Set(Type x, Type y)
	{
		X = x;
		Y = y;
	}

	FX_FORCE_INLINE Type GetX() const { return X; }
	FX_FORCE_INLINE Type GetY() const { return Y; }

	FX_FORCE_INLINE void SetX(Type x) { X = x; }
	FX_FORCE_INLINE void SetY(Type y) { Y = y; }

	Type Width() const { return GetX(); }
	Type Height() const { return GetY(); }

	FX_FORCE_INLINE Type Dot(const Vec2Base& other) const { return X * other.X + Y * other.Y; }
	FX_FORCE_INLINE Type Cross(const Vec2Base& other) const { return X * other.Y - Y * other.X; }

	FX_FORCE_INLINE Type LengthSquared() const { return Dot(*this); }

	FX_FORCE_INLINE Type Length() const
		requires std::floating_point<Type>
	{
		return std::hypotf(X, Y);
	}

	FX_FORCE_INLINE Vec2Base Perpendicular() const { return Vec2Base(-Y, X); }

	FX_FORCE_INLINE bool IsCloseTo(const Vec2Base& other, Type tolerance = static_cast<Type>(0.00001)) const
		requires std::floating_point<Type>
	{
		return std::abs(X - other.X) <= tolerance && std::abs(Y - other.Y) <= tolerance;
	}

public:
	union alignas(16)
	{
		Type mData[2];

		struct
		{
			Type X, Y;
		};
	};
};

// Declaration of Vec2::sZero
template <typename Type>
const Vec2Base<Type> Vec2Base<Type>::sZero = Vec2Base<Type>(0, 0);

using Vec2f = Vec2Base<float32>;
using Vec2d = Vec2Base<float64>;

using Vec2i = Vec2Base<int32>;
using Vec2u = Vec2Base<uint32>;

} // namespace fx

template <>
struct std::formatter<fx::Vec2i>
{
	auto parse(format_parse_context& ctx) { return ctx.begin(); }

	auto format(const fx::Vec2i& obj, std::format_context& ctx) const
	{
		return std::format_to(ctx.out(), "({}, {})", obj.X, obj.Y);
	}
};

template <>
struct std::formatter<fx::Vec2u>
{
	auto parse(format_parse_context& ctx) { return ctx.begin(); }

	auto format(const fx::Vec2u& obj, std::format_context& ctx) const
	{
		return std::format_to(ctx.out(), "({}, {})", obj.X, obj.Y);
	}
};

template <>
struct std::formatter<fx::Vec2f>
{
	auto parse(format_parse_context& ctx) { return ctx.begin(); }

	auto format(const fx::Vec2f& obj, std::format_context& ctx) const
	{
		return std::format_to(ctx.out(), "({:.04}, {:.04})", obj.X, obj.Y);
	}
};


template <>
struct std::formatter<fx::Vec2d>
{
	auto parse(format_parse_context& ctx) { return ctx.begin(); }

	auto format(const fx::Vec2d& obj, std::format_context& ctx) const
	{
		return std::format_to(ctx.out(), "({:.04}, {:.04})", obj.X, obj.Y);
	}
};
