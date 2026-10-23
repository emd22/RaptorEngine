/*
 * File:        Rect.hpp
 * Author:      emd22
 * Created:     14/10/2026
 * Description: 2D rectangle math functions
 */

#pragma once

#include "Vec2.hpp"


namespace fx {


template <typename T>
class Rect
{
public:
	/// Vector2 type to match the type of the rect.
	using Vec2Type = Vec2Base<T>;

public:
	constexpr Rect() = default;
	constexpr Rect(const Vec2Type v_min, const Vec2Type v_max) : Min(v_min), Max(v_max) {}

	/**
	 * @brief Constructor used to convert from one typed Rect to another.
	 */
	template <typename TSrc>
		requires(!std::is_same_v<TSrc, T>)
	constexpr Rect(const Rect<TSrc> src) : Min(Vec2Type::From(src.Min)), Max(Vec2Type::From(src.Max))
	{
	}

	static Rect FromRelative(Vec2Type v_pos, Vec2Type v_dim) { return Rect { v_pos, v_pos + v_dim }; }

	Rect(const Rect& other) = default;
	Rect(Rect&& other) = default;

	Rect& operator=(const Rect& other) = default;
	Rect& operator=(Rect&& other) = default;

	bool operator==(const Rect other) const { return (Min == other.Min && Max == other.Max); }
	bool operator!=(const Rect other) const { return (Min != other.Min || Max != other.Max); }

	constexpr Vec2Type GetCenter() const { return (Min + Max) / Vec2Type { static_cast<T>(2) }; }
	constexpr Vec2Type GetExtent() const { return (Max - Min); }

	constexpr bool Contains(const Vec2Type point) const
	{
		return (point.X >= Min.X && point.X <= Max.X && point.Y >= Min.Y && point.Y <= Max.Y);
	}

	constexpr bool Intersects(const Rect other) const
	{
		const bool x_overlaps = (Min.X <= other.Max.X) && (Max.X >= other.Min.X);
		const bool y_overlaps = (Min.Y <= other.Max.Y) && (Max.Y >= other.Min.Y);
		return (x_overlaps && y_overlaps);
	}

	~Rect() = default;

public:
	// Default values should be the opposite of what they will store.
	Vec2Type Min = Vec2Type::scMaxValue;
	Vec2Type Max = Vec2Type::scMinValue;
};

/**
 * @brief 32-bit floating point rectangle container
 */
using FRect = Rect<float32>;

/**
 * @brief 64-bit floating point rectangle container
 */
using DRect = Rect<double>;

/**
 * @brief 64-bit integer rectangle container
 */
using QRect = Rect<int64>;

using URect = Rect<uint32>;


//
// /**
//  * @brief 2D rectangle container
//  */
// class Rect
// {
// public:
// 	constexpr Rect() = default;
// 	constexpr Rect(const Vec2f v_min, const Vec2f v_max) : Min(v_min), Max(v_max) {}
//
// 	/**
// 	 * @brief Creates a new Rect from position and dimensions as opposed to min and max points.
// 	 */
// 	static Rect FromRelative(Vec2Type v_pos, Vec2Type v_dim) { return Rect { v_pos, v_pos + v_dim }; }
//
// 	Rect(const Rect& other) = default;
// 	Rect(Rect&& other) = default;
//
// 	Rect& operator=(const Rect& other) = default;
// 	Rect& operator=(Rect&& other) = default;
//
// 	bool operator==(const Rect other) const { return (Min == other.Min && Max == other.Max); }
// 	bool operator!=(const Rect other) const { return (Min != other.Min || Max != other.Max); }
//
// 	constexpr Vec2f GetCenter() const { return (Min + Max) * Vec2f(0.5f); }
// 	constexpr Vec2f GetExtent() const { return (Max - Min); }
//
// 	constexpr bool Contains(const Vec2f point) const
// 	{
// 		return (point.X >= Min.X && point.X <= Max.X && point.Y >= Min.Y && point.Y <= Max.Y);
// 	}
//
// 	constexpr bool Intersects(const Rect other) const
// 	{
// 		const bool x_overlaps = (Min.X <= other.Max.X) && (Max.X >= other.Min.X);
// 		const bool y_overlaps = (Min.Y <= other.Max.Y) && (Max.Y >= other.Min.Y);
// 		return (x_overlaps && y_overlaps);
// 	}
//
// 	~Rect() = default;
//
// public:
// 	Vec2f Min = Vec2f(FLT_MAX);
// 	Vec2f Max = Vec2f(-FLT_MAX);
// };
//
// /**
//  * @brief Double precision 2D rectangle container
//  */
// class DRect
// {
// public:
// 	constexpr DRect() = default;
// 	constexpr DRect(const Vec2d v_min, const Vec2d v_max) : Min(v_min), Max(v_max) {}
//
// 	constexpr DRect(const Rect frect) : Min(frect.Min), Max(frect.Max) {}
//
// 	DRect(const DRect& other) = default;
// 	DRect(DRect&& other) = default;
//
// 	DRect& operator=(const DRect& other) = default;
// 	DRect& operator=(DRect&& other) = default;
//
// 	bool operator==(const DRect other) const { return (Min == other.Min && Max == other.Max); }
// 	bool operator!=(const DRect other) const { return (Min != other.Min || Max != other.Max); }
//
// 	constexpr Vec2d GetCenter() const { return (Min + Max) * Vec2d(0.5); }
// 	constexpr Vec2d GetExtent() const { return (Max - Min); }
//
// 	constexpr bool Contains(const Vec2Type point) const
// 	{
// 		return (point.X >= Min.X && point.X <= Max.X && point.Y >= Min.Y && point.Y <= Max.Y);
// 	}
//
// 	constexpr bool Intersects(const DRect other) const
// 	{
// 		const bool x_overlaps = (Min.X <= other.Max.X) && (Max.X >= other.Min.X);
// 		const bool y_overlaps = (Min.Y <= other.Max.Y) && (Max.Y >= other.Min.Y);
// 		return (x_overlaps && y_overlaps);
// 	}
//
// 	~DRect() = default;
//
// public:
// 	Vec2d Min = Vec2d(DBL_MAX);
// 	Vec2d Max = Vec2d(-DBL_MAX);
// };


} // namespace fx
