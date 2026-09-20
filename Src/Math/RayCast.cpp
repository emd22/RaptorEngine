#include "RayCast.hpp"

#include "BoundingBox.hpp"
#include "MathUtil.hpp"
#include "Vec3.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fx {

namespace {

/// Below this the ray is treated as running parallel to a pair of slab planes, rather than dividing by it
constexpr float scParallelEpsilon = 1e-8f;

} // namespace

float RayCast(const Ray& ray, const AABB& aabb)
{
	Vec3f face;
	return RayCast(ray, aabb, face);
}

float RayCast(const Ray& ray, const AABB& aabb, Vec3f& out_face)
{
	out_face = Vec3f::sZero;

	float t_enter = -std::numeric_limits<float>::infinity();
	float t_exit = std::numeric_limits<float>::infinity();

	uint32 enter_axis = 0;
	uint32 exit_axis = 0;
	bool bounded = false;

	for (uint32 axis = 0; axis < 3; axis++) {
		const float origin = ray.Origin.mData[axis];
		const float min = aabb.Min.mData[axis];
		const float max = aabb.Max.mData[axis];

		if (std::fabs(ray.Direction.mData[axis]) < scParallelEpsilon) {
			if (origin < min || origin > max) {
				return scRayCastMiss;
			}

			continue;
		}

		float t_near = (min - origin) * ray.InvDirection.mData[axis];
		float t_far = (max - origin) * ray.InvDirection.mData[axis];

		if (t_near > t_far) {
			std::swap(t_near, t_far);
		}

		if (t_near > t_enter) {
			t_enter = t_near;
			enter_axis = axis;
		}

		if (t_far < t_exit) {
			t_exit = t_far;
			exit_axis = axis;
		}

		bounded = true;

		if (t_exit < t_enter) {
			return scRayCastMiss;
		}
	}

	if (!bounded) {
		return scRayCastMiss;
	}

	// The whole box is behind the origin
	if (t_exit < 0.0f) {
		return scRayCastMiss;
	}

	const bool from_outside = (t_enter > 0.0f);
	const uint32 face_axis = from_outside ? enter_axis : exit_axis;

	const float direction_sign = MathUtil::GetSign(ray.Direction.mData[face_axis]);
	out_face.mData[face_axis] = from_outside ? -direction_sign : direction_sign;

	return from_outside ? t_enter : t_exit;
}

} // namespace fx
