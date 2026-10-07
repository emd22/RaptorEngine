/*
 * File:        Frustum.hpp
 * Author:      emd22
 * Created:     14/09/2026
 * Description: Routines for frustum and frustum related culling
 */

#pragma once

#include <Core/StackArray.hpp>
#include <Math/BoundingBox.hpp>
#include <Math/Mat4.hpp>
#include <Math/OrientedBoundingBox.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec4.hpp>
#include <Object/ObjectLayer.hpp>

namespace fx {

class PerspectiveCamera;
class Camera;

enum class eFrustumPlane
{
	Left = 0,
	Right = 1,
	Bottom = 2,
	Top = 3,
	Near = 4,
	Far = 5
};

constexpr uint32 FrustumPlaneBit(eFrustumPlane plane) { return 1U << static_cast<uint32>(plane); }

constexpr uint32 scFrustumAllPlanes = 0x3F;

constexpr uint32 scFrustumSidePlanes = FrustumPlaneBit(eFrustumPlane::Left) | FrustumPlaneBit(eFrustumPlane::Right) |
									   FrustumPlaneBit(eFrustumPlane::Bottom) | FrustumPlaneBit(eFrustumPlane::Top);

class Frustum
{
public:
	Frustum() = default;

	void Rebuild(const PerspectiveCamera& camera);
	void Rebuild(const Camera& camera, eObjectLayer layer);

	void Rebuild(const Mat4f& view_projection);

	bool TileIntersectsAABB(const AABB& tile_aabb) const;

	/// Whether any part of the sphere is inside of the frustum. Can let through spheres that are just outside of a
	/// corner.
	bool IntersectsSphere(const Vec3f center, float32 radius, uint32 plane_mask = scFrustumAllPlanes) const;

	bool IntersectsAABB(const AABB& aabb, uint32 plane_mask = scFrustumAllPlanes) const;

	bool IntersectsOBB(const OBB& obb, uint32 plane_mask = scFrustumAllPlanes) const;

	FX_FORCE_INLINE const Vec4f& GetPlane(const eFrustumPlane plane) const
	{
		return mClipPlanes[static_cast<uint32>(plane)];
	}

	AABB GetFrustumBoundingBox(const PerspectiveCamera& camera);

private:
	StackArray<Vec4f, 6> mClipPlanes;

	float32 mFrustumY = 0.0f;
};

} // namespace fx
\
