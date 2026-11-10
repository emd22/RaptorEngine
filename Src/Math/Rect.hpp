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
class RectBase
{
public:
	/// Vector2 type to match the type of the rect.
	using Vec2Type = Vec2Base<T>;

public:
	constexpr RectBase() = default;
	constexpr RectBase(const Vec2Type v_min, const Vec2Type v_max) : Min(v_min), Max(v_max) {}

	/**
	 * @brief Constructor used to convert from one typed Rect to another.
	 */
	template <typename TSrc>
		requires(!std::is_same_v<TSrc, T>)
	constexpr RectBase(const RectBase<TSrc> src) : Min(Vec2Type::From(src.Min)), Max(Vec2Type::From(src.Max))
	{
	}

	static RectBase FromRelative(Vec2Type v_pos, Vec2Type v_dim) { return RectBase { v_pos, v_pos + v_dim }; }

	RectBase(const RectBase& other) = default;
	RectBase(RectBase&& other) = default;

	RectBase& operator=(const RectBase& other) = default;
	RectBase& operator=(RectBase&& other) = default;

	bool operator==(const RectBase other) const { return (Min == other.Min && Max == other.Max); }
	bool operator!=(const RectBase other) const { return (Min != other.Min || Max != other.Max); }


	constexpr RectBase OffsetBy(const Vec2Type offset) { return RectBase(Min + offset, Max + offset); }


	constexpr Vec2Type GetCenter() const { return (Min + Max) / Vec2Type { static_cast<T>(2) }; }
	constexpr Vec2Type GetExtent() const { return (Max - Min); }

	constexpr bool Contains(const Vec2Type point) const
	{
		return (point.X >= Min.X && point.X <= Max.X && point.Y >= Min.Y && point.Y <= Max.Y);
	}

	constexpr bool Intersects(const RectBase other) const
	{
		const bool x_overlaps = (Min.X <= other.Max.X) && (Max.X >= other.Min.X);
		const bool y_overlaps = (Min.Y <= other.Max.Y) && (Max.Y >= other.Min.Y);
		return (x_overlaps && y_overlaps);
	}

	~RectBase() = default;

public:
	// Default values should be the opposite of what they will store.
	Vec2Type Min = Vec2Type::scMaxValue;
	Vec2Type Max = Vec2Type::scMinValue;
};

/**
 * @brief 32-bit floating point rectangle container
 */
using FRect = RectBase<float32>;

/**
 * @brief 64-bit floating point rectangle container
 */
using DRect = RectBase<double>;

/**
 * @brief 64-bit integer rectangle container
 */
using QRect = RectBase<int64>;

using IRect = RectBase<int32>;
using URect = RectBase<uint32>;


} // namespace fx
