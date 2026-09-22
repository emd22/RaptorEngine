#include "OrientedBoundingBox.hpp"

#include "Vec4.hpp"

#include <limits>

namespace fx {

OBB OBB::FromLocalBounds(const AABB& local_bounds, const Mat4f& transform)
{
	OBB obb;

	for (uint32 corner = 0; corner < 8; corner++) {
		const Vec4f local((corner & 1) ? local_bounds.Max.X : local_bounds.Min.X,
						  (corner & 2) ? local_bounds.Max.Y : local_bounds.Min.Y,
						  (corner & 4) ? local_bounds.Max.Z : local_bounds.Min.Z, 1.0f);

		const Vec4f world = transform * local;
		obb.Corners[corner] = Vec3f(world.X, world.Y, world.Z);
	}

	return obb;
}

AABB OBB::GetWorldAABB() const
{
	Vec3f out_min(std::numeric_limits<float32>::max());
	Vec3f out_max(-std::numeric_limits<float32>::max());

	for (const Vec3f& corner : Corners) {
		out_min = Vec3f::Min(out_min, corner);
		out_max = Vec3f::Max(out_max, corner);
	}

	return AABB(out_min, out_max);
}

} // namespace fx
