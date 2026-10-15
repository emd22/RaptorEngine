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

	constexpr Vec2Base(Type x, Type y) : X(x), Y(y) {}
	constexpr Vec2Base(Type scalar) : X(scalar), Y(scalar) {}

	constexpr Vec2Base(const Vec2Base& other) : X(other.X), Y(other.Y) {}
	constexpr explicit Vec2Base(const Vec2Base<float>& fvec)
		requires std::is_same_v<Type, double>
		: X(static_cast<double>(fvec.X)), Y(static_cast<double>(fvec.Y))
	{
	}

	constexpr explicit Vec2Base(const Vec2Base<double>& dvec)
		requires std::is_same_v<Type, float>
		: X(static_cast<float>(dvec.X)), Y(static_cast<float>(dvec.Y))
	{
	}

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

	Vec2Base& operator=(const Vec2Base& other)
	{
		X = other.X;
		Y = other.Y;
		return *this;
	}

	constexpr Vec2Base& operator=(const Vec2Base<float>& fvec)
		requires std::is_same_v<Type, double>

	{
		X = (static_cast<double>(fvec.X));
		Y = (static_cast<double>(fvec.Y));
		return *this;
	}

	constexpr Vec2Base& operator=(const Vec2Base<double>& dvec)
		requires std::is_same_v<Type, float>
	{
		X = (static_cast<float>(dvec.X));
		Y = (static_cast<float>(dvec.Y));
		return *this;
	}

	template <int TX, int TY>
	static FX_FORCE_INLINE Vec2Base FlipSigns(const Vec2Base vec)
	{
		const Type rx = TX > 0 ? vec.X : -vec.X;
		const Type ry = TY > 0 ? vec.Y : -vec.Y;

		return Vec2Base(rx, ry);
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
		requires std::is_same_v<Type, float>
	{
		return std::hypotf(X, Y);
	}

	FX_FORCE_INLINE Type Length() const
		requires std::is_same_v<Type, double>
	{
		return std::hypot(X, Y);
	}

	static FX_FORCE_INLINE Vec2Base Min(Vec2Base a, Vec2Base b)
	{
		return Vec2Base(std::min(a.X, b.X), std::min(a.Y, b.Y));
	}


	static FX_FORCE_INLINE Vec2Base Max(Vec2Base a, Vec2Base b)
	{
		return Vec2Base(std::max(a.X, b.X), std::max(a.Y, b.Y));
	}

	FX_FORCE_INLINE Vec2Base Perpendicular() const { return Vec2Base(-Y, X); }

	FX_FORCE_INLINE bool IsCloseTo(const Vec2Base& other, Type tolerance = static_cast<Type>(0.00001)) const
		requires std::floating_point<Type>
	{
		return std::abs(X - other.X) <= tolerance && std::abs(Y - other.Y) <= tolerance;
	}

public:
	union
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
