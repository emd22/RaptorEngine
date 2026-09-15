#include "Frustum.hpp"

#include "NeonUtil.hpp"
#include "SIMDHelper.hpp"

#include <Renderer/Camera.hpp>


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

	mLeftPlane = vr_w + vr_x;
	mRightPlane = vr_w - vr_x;

	mBottomPlane = vr_w + vr_y;
	mTopPlane = vr_w - vr_y;

	mNearPlane = vr_w - vr_z;
	mFarPlane = vr_z;

	mFrustumY = 0.0f;
}

bool Frustum::IsTileVisible(float x, float z) const
{
	FLOAT4 v_pos = simd::LoadFloat4(x, mFrustumY, z, 1.0f);

	if ((Neon::Dot(mLeftPlane.mIntrin, v_pos) < 0.0f) ||  /* */
		(Neon::Dot(mRightPlane.mIntrin, v_pos) < 0.0f) || /* */
		(Neon::Dot(mNearPlane.mIntrin, v_pos) < 0.0f) ||  /* */
		(Neon::Dot(mFarPlane.mIntrin, v_pos) < 0.0f)) {
		return false;
	}

	return true;
}


Frustum::Coverage Frustum::GetFrustumCoverage(const PerspectiveCamera& camera)
{
	// General idea here: instead of checking per world tile if it intersects with the frustum, get the points for the
	// frustum in all directions and get the range of visible tiles from there.

	Mat4f vp_matrix = camera.GetCameraMatrix(eObjectLayer::WorldLayer);
	Mat4f vp_inverse = vp_matrix.Inverse();

	const Vec3f camera_pos = camera.Position;

	constexpr float32 cGroundY = 0.0f;
	constexpr float32 cEpsilon = 1e-6f;

	// NDC corners in screen space. Order is CCW starting at bottom left
	static constexpr float32 scNdcCorners[4][2] = {
		{ -1.0f, -1.0f },
		{ 1.0f, -1.0f },
		{ 1.0f, 1.0f },
		{ -1.0f, 1.0f },
	};

	Coverage coverage;

	for (uint32 i = 0; i < 4; i++) {
		Vec4f ndc(scNdcCorners[i][0], scNdcCorners[i][1], 1.0f, 1.0f);
		Vec4f world_h = vp_inverse.MultiplyVec4f(ndc);

		if (std::fabs(world_h.W) < cEpsilon) {
			coverage.Corners[i] = Vec2f(camera_pos.X, camera_pos.Z);
			continue;
		}

		const float32 inv_w = 1.0f / world_h.W;
		Vec3f far_point(world_h.X * inv_w, world_h.Y * inv_w, world_h.Z * inv_w);

		Vec3f ray = far_point - camera_pos;

		// Ray parallel to the ground plane: use the far point itself. This is the case that the camera is exactly level
		// or the corners match the horizon.
		if (ray.Y > -cEpsilon && ray.Y < cEpsilon) {
			coverage.Corners[i] = Vec2f(far_point.X, far_point.Z);
			continue;
		}

		const float32 travel = (cGroundY - camera_pos.Y) / ray.Y;

		if (travel < 0.0f) {
			coverage.Corners[i] = Vec2f(far_point.X, far_point.Z);
			continue;
		}

		Vec3f hit = camera_pos + (ray * Vec3f(travel));

		coverage.Corners[i] = Vec2f(hit.X, hit.Z);
	}

	return coverage;
}


} // namespace fx
