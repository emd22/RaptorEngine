#pragma once

#include <Math/FMat4.hpp>
#include <Math/FQuat.hpp>
#include <Math/FVec3.hpp>
#include <Object/ObjectLayer.hpp>
#include <algorithm>
#include <cassert>
#include <cmath>


namespace fx {

struct ExposureSettings
{
	static constexpr float32 scMinValue = 1e-4f;
	static constexpr float32 scMeterCalibration = 1.2f;

public:
	float32 GetEV100() const
	{
		const float32 aperture = std::max(Aperture, scMinValue);
		const float32 shutter = std::max(ShutterTime, scMinValue);
		const float32 iso = std::max(ISO, scMinValue);

		return std::log2((aperture * aperture / shutter) * (100.0f / iso));
	}

	float32 GetExposure() const { return std::exp2(Compensation) / (scMeterCalibration * std::exp2(GetEV100())); }

public:
	float32 Aperture = 16.0f;
	float32 ShutterTime = 0.01f;
	float32 ISO = 100.0f;
	float32 Compensation = 0.0f;
};


class BaseCamera
{
public:
	BaseCamera() = default;
	BaseCamera(const FVec3 position, const FQuat rotation, float32 z_near, float32 z_far)
		: mPosition(position), mRotation(rotation), mZNearClip(z_near), mZFarClip(z_far)
	{
		// Since Raptor uses reverse-Z, the far plane must be smaller than the near plane.
		assert(z_near > z_far);
	}

	virtual bool Update() = 0;

	FX_FORCE_INLINE FVec3 GetForwardVector() const { return mRotation.GetDirection(); }
	FX_FORCE_INLINE FVec3 GetRightVector() const { return FVec3::sRight.Rotate(mRotation); }
	FX_FORCE_INLINE FVec3 GetUpVector() const { return FVec3::sUp.Rotate(mRotation); }

	FX_FORCE_INLINE void SetClip(float32 z_near, float32 z_far)
	{
		mZNearClip = z_near;
		mZFarClip = z_far;
		mbRequireProjectionMatrixUpdate = true;
	}

	/**
	 * @brief Returns forward facing vector, disregarding influence from the Y axis. This is to prevent pitch from
	 * slowing down player movement for example.
	 */
	FX_FORCE_INLINE FVec3 GetFlatForwardVector() const { return GetRightVector().Cross(FVec3::sUp).Normalize(); }

	/**
	 * @brief Returns right facing vector, disregarding influence from the Y axis.
	 */
	FX_FORCE_INLINE FVec3 GetFlatRightVector() const { return FVec3::sUp.Cross(GetFlatForwardVector()); }

	/**
	 * @brief Moves the camera in world space, disregarding any directionality.
	 */
	FX_FORCE_INLINE void MoveWS(const FVec3 offset)
	{
		mPosition += offset;
		mbTransformDirty = true;
	}

	FX_FORCE_INLINE void SetPosition(const FVec3 position)
	{
		mPosition = position;
		mbTransformDirty = true;
	}


	FX_FORCE_INLINE void SetRotation(const FQuat rotation)
	{
		mRotation = rotation;
		mbTransformDirty = true;
	}

	FX_FORCE_INLINE FVec3 GetPosition() const { return mPosition; }
	FX_FORCE_INLINE FQuat GetRotation() const { return mRotation; }

	void LookAt(const FVec3 target, const FVec3 up = FVec3::sUp);

	virtual const FMat4& GetCameraMatrix(eObjectLayer layer) const { return mCameraMatrix; }

	FX_FORCE_INLINE const FMat4& GetViewMatrix() const { return mViewMatrix; }
	FX_FORCE_INLINE const FMat4& GetProjectionMatrix() const { return mProjectionMatrix; }

	FX_FORCE_INLINE float32 GetNearClip() const { return mZNearClip; }
	FX_FORCE_INLINE float32 GetFarClip() const { return mZFarClip; }

	virtual ~BaseCamera() = default;

protected:
	void RebuildViewMatrix();

protected:
	/// The result matrix for the camera.
	FMat4 mCameraMatrix = FMat4::scIdentity;

	FMat4 mProjectionMatrix = FMat4::scIdentity;
	FMat4 mViewMatrix = FMat4::scIdentity;

	FVec3 mPosition = FVec3::sZero;
	FQuat mRotation = FQuat::scIdentity;


	/// True if the position or rotation has changed
	bool mbTransformDirty = true;

	bool mbRequireProjectionMatrixUpdate = true;

	float32 mZNearClip = 750.0f;
	float32 mZFarClip = 0.01f;
};


} // namespace fx
