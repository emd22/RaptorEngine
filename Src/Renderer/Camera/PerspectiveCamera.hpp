#pragma once

#include "BaseCamera.hpp"

#include <Math/MathUtil.hpp>

namespace fx {

class PerspectiveCamera2 : public BaseCamera
{
public:
	PerspectiveCamera2() = default;

	PerspectiveCamera2(float32 fov_y, float32 aspect, float32 z_near, float32 z_far)
		: mFovY(fov_y), mAspectRatio(aspect)
	{
		SetClip(z_near, z_far);
	}

	bool Update() override;

	FX_FORCE_INLINE void SetFOV(const float32 fov_y)
	{
		mFovY = fov_y;
		mbRequireProjectionMatrixUpdate = true;
	}

	FX_FORCE_INLINE float32 GetFOV() const { return mFovY; }

	FX_FORCE_INLINE void SetAspectRatio(const float32 aspect)
	{
		mAspectRatio = aspect;
		mbRequireProjectionMatrixUpdate = true;
	}

	FX_FORCE_INLINE float32 GetAspectRatio() const { return mAspectRatio; }

	FX_FORCE_INLINE const Mat4f& GetInvViewMatrix() const { return mInvViewMatrix; }
	FX_FORCE_INLINE const Mat4f& GetInvProjectionMatrix() const { return mInvProjectionMatrix; }

	~PerspectiveCamera2() override = default;

protected:
	// The inverse matrices for view and projection. This isn't on the BaseCamera class as we seldom need them for
	// anything except for probe baking and some graphical effects.
	Mat4f mInvViewMatrix = Mat4f::scIdentity;
	Mat4f mInvProjectionMatrix = Mat4f::scIdentity;

	float32 mFovY = MathUtil::DegreesToRadians(80.0f);
	float32 mAspectRatio = 1.0f;
};


} // namespace fx
