#pragma once

#include "Vec3.hpp"

namespace fx {

class AABB;

struct Ray
{
	Ray() = default;

	Ray(const Vec3f& origin, const Vec3f& direction)
		: Origin(origin), Direction(direction), InvDirection(1.0f / direction.X, 1.0f / direction.Y, 1.0f / direction.Z)
	{
	}

	Vec3f Origin;
	Vec3f Direction;
	Vec3f InvDirection;
};

/// Returned by RayCast() when the ray never meets the box
inline constexpr float scRayCastMiss = -1.0f;

float RayCast(const Ray& ray, const AABB& aabb);

/**
 * @brief As RayCast(), and also reports which face the ray met.
 *
 * `out_face` is the outward normal of that face, axis aligned in the box's own space, and zero on a miss. A ray
 * starting inside the box gets the face it leaves through, which is the one it is pointing at.
 */
float RayCast(const Ray& ray, const AABB& aabb, Vec3f& out_face);

} // namespace fx
