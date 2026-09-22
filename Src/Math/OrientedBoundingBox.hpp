#pragma once

#include "BoundingBox.hpp"
#include "Mat4.hpp"
#include "Vec3.hpp"

namespace fx {

class OBB
{
public:
	OBB() = default;

	static OBB FromLocalBounds(const AABB& local_bounds, const Mat4f& transform);
	AABB GetWorldAABB() const;

public:
	Vec3f Corners[8];
};

} // namespace fx
