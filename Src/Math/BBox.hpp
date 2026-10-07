#pragma once

#include "Mat4.hpp"
#include "Vec3.hpp"

namespace fx {

class BBox
{
public:
	BBox() = default;
	BBox(Vec3f min, Vec3f max);

	FX_FORCE_INLINE Vec3f GetSize() { return Max - Min; }

	void Add(const BBox& other);

	BBox& OffsetBy(const Vec3f offset)
	{
		Min += offset;
		Max += offset;
		return *this;
	}

	BBox operator+(const BBox& other) const;
	BBox& operator+=(const BBox& other);

	BBox& operator=(const BBox& other);

public:
	Vec3f Min = Vec3f::sZero;
	Vec3f Max = Vec3f::sZero;
};

class OBBox
{
public:
	OBBox() = default;

	static OBBox FromLocalBounds(const BBox& local_bounds, const Mat4f transform);
	BBox GetWorldAABB() const;

public:
	Vec3f Corners[8];
};

} // namespace fx
