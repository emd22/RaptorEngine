/*
 * File:        Frustum.hpp
 * Author:      emd22
 * Created:     14/09/2026
 * Description: Routines for frustum and frustum related culling
 */

#pragma once

#include <Math/Vec2.hpp>
#include <Math/Vec4.hpp>

namespace fx {

class PerspectiveCamera;

class Frustum
{
public:
	struct Coverage
	{
		Vec2f Corners[4];
	};

public:
	Frustum() = default;

	void Rebuild(const PerspectiveCamera& camera);

	/**
	 * @brief Returns true if a top-down 2D position is visible from this frustum.
	 */
	bool IsTileVisible(float x, float z) const;

	Coverage GetFrustumCoverage(const PerspectiveCamera& camera);

private:
	Vec4f mLeftPlane;
	Vec4f mRightPlane;
	Vec4f mTopPlane;
	Vec4f mBottomPlane;
	Vec4f mNearPlane;
	Vec4f mFarPlane;

	float32 mFrustumY = 0.0f;
};

} // namespace fx
