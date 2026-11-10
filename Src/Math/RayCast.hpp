#pragma once

#include "FVec3.hpp"

namespace fx {

class BBox;

struct Ray
{
	Ray() = default;

	Ray(const FVec3 origin, const FVec3 direction)
		: Origin(origin), Direction(direction), InvDirection(1.0f / direction.X, 1.0f / direction.Y, 1.0f / direction.Z)
	{
	}

	FVec3 Origin;
	FVec3 Direction;
	FVec3 InvDirection;
};

/// Returned by RayCast() when the ray never meets the box
inline constexpr float scRayCastMiss = -1.0f;

float RayCast(const Ray& ray, const BBox& aabb);

/**
 * @brief As RayCast(), and also reports which face the ray met.
 *
 * `out_face` is the outward normal of that face, axis aligned in the box's own space, and zero on a miss. A ray
 * starting inside the box gets the face it leaves through, which is the one it is pointing at.
 */
float RayCast(const Ray& ray, const BBox& aabb, FVec3& out_face);

} // namespace fx
