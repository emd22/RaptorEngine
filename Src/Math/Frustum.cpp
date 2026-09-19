#include "Frustum.hpp"

#include "NeonUtil.hpp"
#include "SIMDHelper.hpp"

#include <Renderer/Camera.hpp>
#include <cfloat>

#define PLANE(idx_) mClipPlanes[static_cast<uint32>(idx_)]

namespace fx {
// We need to find the tiles that can possibly intersect with the frustum.

static Vec4f ToPlane(const Vec4f& vec)
{
	float32 length = Vec3f(vec.mIntrin).Length();
	return (Vec4f(vec) / length);
}


void Frustum::Rebuild(const PerspectiveCamera& camera)
{
	// Note that `.Transposed()` is faster than rebuilding each vector manually.
	Mat4f vp_m = camera.GetCameraMatrix(eObjectLayer::WorldLayer).Transposed();

	const Vec4f& vr_x = vp_m.Rows[0];
	const Vec4f& vr_y = vp_m.Rows[1];
	const Vec4f& vr_z = vp_m.Rows[2];
	const Vec4f& vr_w = vp_m.Rows[3];

	PLANE(eFrustumPlane::Left) = ToPlane(vr_w + vr_x);
	PLANE(eFrustumPlane::Right) = ToPlane(vr_w - vr_x);

	PLANE(eFrustumPlane::Bottom) = ToPlane(vr_w + vr_y);
	PLANE(eFrustumPlane::Top) = ToPlane(vr_w - vr_y);

	// Note that this is for reversed-z
	PLANE(eFrustumPlane::Near) = ToPlane(vr_w - vr_z);
	PLANE(eFrustumPlane::Far) = ToPlane(vr_z);

	mFrustumY = 0.0f;
}

bool Frustum::TileIntersectsAABB(const AABB& tile_aabb) const
{
	// We only need to check left right, near and far here since we treat tiles as 2D (X and Z).

	static constexpr int scPlanesToTest[4] = {
		static_cast<int>(eFrustumPlane::Left),
		static_cast<int>(eFrustumPlane::Right),
		static_cast<int>(eFrustumPlane::Near),
		static_cast<int>(eFrustumPlane::Far),
	};

	for (int i : scPlanesToTest) {
		const Vec4f& plane = mClipPlanes[i];

		// Positive vertex: the AABB corner furthest along the plane normal.
		Vec3f p_vertex(plane.X >= 0.0f ? tile_aabb.Max.X : tile_aabb.Min.X,
					   plane.Y >= 0.0f ? tile_aabb.Max.Y : tile_aabb.Min.Y,
					   plane.Z >= 0.0f ? tile_aabb.Max.Z : tile_aabb.Min.Z);

		if (Vec3f(plane).Dot(p_vertex) + plane.W < 0.0f) {
			return false;
		}
	}

	return true;
}

bool Frustum::IntersectsSphere(const Vec3f& center, float32 radius) const
{
	// Rebuild() writes the planes by index, so `mClipPlanes.Size` stays at zero
	for (uint32 i = 0; i <= static_cast<uint32>(eFrustumPlane::Far); i++) {
		const Vec4f& plane = mClipPlanes[i];

		// The planes are normalized, so this is the signed distance to the plane
		if (Vec3f(plane).Dot(center) + plane.W < -radius) {
			return false;
		}
	}

	return true;
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
