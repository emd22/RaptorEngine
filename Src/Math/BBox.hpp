#pragma once

#include "FMat4.hpp"
#include "FVec3.hpp"

namespace fx {

/**
 * @brief Axis-aligned bounding box (e.g. just minimum point and maximum point)
 */
class BBox
{
public:
	BBox() = default;
	BBox(FVec3 min, FVec3 max);

	FX_FORCE_INLINE FVec3 GetSize() { return Max - Min; }

	void Add(const BBox& other);

	BBox& OffsetBy(const FVec3 offset)
	{
		Min += offset;
		Max += offset;
		return *this;
	}

	BBox AddMargin(const FVec3 margin) const;

	BBox operator+(const BBox& other) const;
	BBox& operator+=(const BBox& other);

	BBox& operator=(const BBox& other);

public:
	FVec3 Min = FVec3::sZero;
	FVec3 Max = FVec3::sZero;
};

/**
 * @brief Oriented bounding box. A bounding box that has points oriented with scale, rotation and position built from
 * points.
 */
class OBBox
{
public:
	OBBox() = default;

	static OBBox FromLocalBounds(const BBox& local_bounds, const FMat4 transform);
	BBox GetWorldAABB() const;

public:
	FVec3 Corners[8];
};

} // namespace fx
