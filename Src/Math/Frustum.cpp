#include "Frustum.hpp"

#include "NeonUtil.hpp"
#include "SIMDHelper.hpp"

#include <Renderer/Camera.hpp>
#include <cfloat>

#define PLANE(idx_) mClipPlanes[static_cast<uint32>(idx_)]

namespace fx {
// We need to find the tiles that can possibly intersect with the frustum.


void Frustum::Rebuild(const PerspectiveCamera& camera)
{
	// Note that `.Transposed()` is faster than rebuilding each vector manually.
	Mat4f vp_m = camera.GetCameraMatrix(eObjectLayer::WorldLayer).Transposed();

	const Vec4f& vr_x = vp_m.Rows[0];
	const Vec4f& vr_y = vp_m.Rows[1];
	const Vec4f& vr_z = vp_m.Rows[2];
	const Vec4f& vr_w = vp_m.Rows[3];

	PLANE(eFrustumPlane::Left) = vr_w + vr_x;
	PLANE(eFrustumPlane::Right) = vr_w - vr_x;

	PLANE(eFrustumPlane::Bottom) = vr_w + vr_y;
	PLANE(eFrustumPlane::Top) = vr_w - vr_y;

	PLANE(eFrustumPlane::Near) = vr_w - vr_z;
	PLANE(eFrustumPlane::Far) = vr_z;

	mFrustumY = 0.0f;
}


AABB Frustum::GetFrustumBoundingBox(const PerspectiveCamera& camera)
{
	Mat4f vp_matrix = camera.GetCameraMatrix(eObjectLayer::WorldLayer);
	Mat4f vp_inverse = vp_matrix.Inverse();

	static const Vec3f ndc_corners[8] = { { -1, -1, 0 }, { 1, -1, 0 }, { 1, 1, 0 }, { -1, 1, 0 },
										  { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } };

	Vec3f min_point(FLT_MAX, FLT_MAX, FLT_MAX);
	Vec3f max_point(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (const Vec3f& corner : ndc_corners) {
		Vec4f world = vp_inverse * Vec4f(corner.X, corner.Y, corner.Z, 1.0f);
		world = (world / world.W);

		min_point = Vec3f::Min(min_point, Vec3f(world.X, world.Y, world.Z));
		max_point = Vec3f::Max(max_point, Vec3f(world.X, world.Y, world.Z));
	}

	return AABB(min_point, max_point);
}


} // namespace fx
