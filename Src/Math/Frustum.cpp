#include "Frustum.hpp"

#include "NeonUtil.hpp"
#include "SIMDHelper.hpp"

#include <Renderer/Camera/PerspectiveCamera.hpp>
#include <cfloat>
#include <cmath>

#define PLANE(idx_) mClipPlanes[static_cast<uint32>(idx_)]

namespace fx {
// We need to find the tiles that can possibly intersect with the frustum.

static FVec4 ToPlane(const FVec4 vec)
{
	float32 length = FVec3(vec.mIntrin).Length();
	return (FVec4(vec) / length);
}


void Frustum::Rebuild(const PerspectiveCamera& camera) { Rebuild(camera.GetCameraMatrix(eObjectLayer::WorldLayer)); }

void Frustum::Rebuild(const BaseCamera& camera, eObjectLayer layer) { Rebuild(camera.GetCameraMatrix(layer)); }

void Frustum::Rebuild(const FMat4& view_projection)
{
	// Note that `.Transposed()` is faster than rebuilding each vector manually.
	FMat4 vp_m = view_projection.Transposed();

	const FVec4 vr_x = vp_m.Rows[0];
	const FVec4 vr_y = vp_m.Rows[1];
	const FVec4 vr_z = vp_m.Rows[2];
	const FVec4 vr_w = vp_m.Rows[3];

	PLANE(eFrustumPlane::Left) = ToPlane(vr_w + vr_x);
	PLANE(eFrustumPlane::Right) = ToPlane(vr_w - vr_x);

	PLANE(eFrustumPlane::Bottom) = ToPlane(vr_w + vr_y);
	PLANE(eFrustumPlane::Top) = ToPlane(vr_w - vr_y);

	// Note that this is for reversed-z
	PLANE(eFrustumPlane::Near) = ToPlane(vr_w - vr_z);
	PLANE(eFrustumPlane::Far) = ToPlane(vr_z);

	mFrustumY = 0.0f;
}

bool Frustum::TileIntersectsAABB(const BBox& tile_aabb) const
{
	// We only need to check left right, near and far here since we treat tiles as 2D (X and Z).

	static constexpr int scPlanesToTest[4] = {
		static_cast<int>(eFrustumPlane::Left),
		static_cast<int>(eFrustumPlane::Right),
		static_cast<int>(eFrustumPlane::Near),
		static_cast<int>(eFrustumPlane::Far),
	};

	for (int i : scPlanesToTest) {
		const FVec4 plane = mClipPlanes[i];

		// Positive vertex: the AABB corner furthest along the plane normal.
		FVec3 p_vertex(plane.X >= 0.0f ? tile_aabb.Max.X : tile_aabb.Min.X,
					   plane.Y >= 0.0f ? tile_aabb.Max.Y : tile_aabb.Min.Y,
					   plane.Z >= 0.0f ? tile_aabb.Max.Z : tile_aabb.Min.Z);

		if (FVec3(plane).Dot(p_vertex) + plane.W < 0.0f) {
			return false;
		}
	}

	return true;
}

bool Frustum::IntersectsSphere(const FVec3 center, float32 radius, uint32 plane_mask) const
{
	// Rebuild() writes the planes by index, so `mClipPlanes.Size` stays at zero
	for (uint32 i = 0; i <= static_cast<uint32>(eFrustumPlane::Far); i++) {
		if ((plane_mask & (1U << i)) == 0) {
			continue;
		}

		const FVec4 plane = mClipPlanes[i];

		// The planes are normalized, so this is the signed distance to the plane
		if (FVec3(plane).Dot(center) + plane.W < -radius) {
			return false;
		}
	}

	return true;
}

bool Frustum::IntersectsAABB(const BBox& aabb, uint32 plane_mask) const
{
	const FVec3 center = (aabb.Min + aabb.Max) * 0.5f;
	const FVec3 half_extent = (aabb.Max - aabb.Min) * 0.5f;

	for (uint32 i = 0; i <= static_cast<uint32>(eFrustumPlane::Far); i++) {
		if ((plane_mask & (1U << i)) == 0) {
			continue;
		}

		const FVec4 plane = mClipPlanes[i];
		const FVec3 normal(plane);

		const float32 reach = half_extent.Dot(normal.Abs());

		if (normal.Dot(center) + plane.W < -reach) {
			return false;
		}
	}

	return true;
}

bool Frustum::IntersectsOBB(const OBBox& obb, uint32 plane_mask) const
{
	const FVec3 center = (obb.Corners[0] + obb.Corners[7]) * 0.5f;
	const FVec3 half_x = (obb.Corners[1] - obb.Corners[0]) * 0.5f;
	const FVec3 half_y = (obb.Corners[2] - obb.Corners[0]) * 0.5f;
	const FVec3 half_z = (obb.Corners[4] - obb.Corners[0]) * 0.5f;

	for (uint32 i = 0; i <= static_cast<uint32>(eFrustumPlane::Far); i++) {
		if ((plane_mask & (1U << i)) == 0) {
			continue;
		}

		const FVec4 plane = mClipPlanes[i];
		const FVec3 normal(plane);

		const float32 reach = std::abs(half_x.Dot(normal)) + std::abs(half_y.Dot(normal)) +
							  std::abs(half_z.Dot(normal));

		if (normal.Dot(center) + plane.W < -reach) {
			return false;
		}
	}

	return true;
}


BBox Frustum::GetFrustumBoundingBox(const PerspectiveCamera& camera)
{
	FMat4 vp_matrix = camera.GetCameraMatrix(eObjectLayer::WorldLayer);
	FMat4 vp_inverse = vp_matrix.Inverse();

	static const FVec3 ndc_corners[8] = { { -1, -1, 0 }, { 1, -1, 0 }, { 1, 1, 0 }, { -1, 1, 0 },
										  { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } };

	FVec3 min_point(FLT_MAX, FLT_MAX, FLT_MAX);
	FVec3 max_point(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (const FVec3 corner : ndc_corners) {
		FVec4 world = vp_inverse * FVec4(corner.X, corner.Y, corner.Z, 1.0f);
		world = (world / world.W);

		min_point = FVec3::Min(min_point, FVec3(world.X, world.Y, world.Z));
		max_point = FVec3::Max(max_point, FVec3(world.X, world.Y, world.Z));
	}

	return BBox(min_point, max_point);
}


} // namespace fx
