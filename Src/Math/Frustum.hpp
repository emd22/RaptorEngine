/*
 * File:        Frustum.hpp
 * Author:      emd22
 * Created:     14/09/2026
 * Description: Routines for frustum and frustum related culling
 */

#pragma once

#include <Core/StackArray.hpp>
#include <Math/BoundingBox.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec4.hpp>

namespace fx {

class PerspectiveCamera;

enum class eFrustumPlane
{
	Left = 0,
	Right = 1,
	Bottom = 2,
	Top = 3,
	Near = 4,
	Far = 5
};

class Frustum
{
public:
	Frustum() = default;

	void Rebuild(const PerspectiveCamera& camera);

	bool TileIntersectsAABB(const AABB& tile_aabb) const;

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
