#pragma once

#include <Math/Mat4.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
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
	BaseCamera(const Vec3f position, const Quat rotation, float32 z_near, float32 z_far)
		: mPosition(position), mRotation(rotation), mZNearClip(z_near), mZFarClip(z_far)
	{
		// Since Raptor uses reverse-Z, the far plane must be smaller than the near plane.
		assert(z_near > z_far);
	}

	virtual bool Update() = 0;

	FX_FORCE_INLINE Vec3f GetForwardVector() const { return mRotation.GetDirection(); }
	FX_FORCE_INLINE Vec3f GetRightVector() const { return Vec3f::sRight.Rotate(mRotation); }
	FX_FORCE_INLINE Vec3f GetUpVector() const { return Vec3f::sUp.Rotate(mRotation); }

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
	FX_FORCE_INLINE Vec3f GetFlatForwardVector() const { return GetRightVector().Cross(Vec3f::sUp).Normalize(); }

	/**
	 * @brief Returns right facing vector, disregarding influence from the Y axis.
	 */
	FX_FORCE_INLINE Vec3f GetFlatRightVector() const { return Vec3f::sUp.Cross(GetFlatForwardVector()); }

	/**
	 * @brief Moves the camera in world space, disregarding any directionality.
	 */
	FX_FORCE_INLINE void MoveWS(const Vec3f offset)
	{
		mPosition += offset;
		mbTransformDirty = true;
	}

	FX_FORCE_INLINE void SetPosition(const Vec3f position)
	{
		mPosition = position;
		mbTransformDirty = true;
	}


	FX_FORCE_INLINE void SetRotation(const Quat rotation)
	{
		mRotation = rotation;
		mbTransformDirty = true;
	}

	FX_FORCE_INLINE Vec3f GetPosition() const { return mPosition; }
	FX_FORCE_INLINE Quat GetRotation() const { return mRotation; }

	void LookAt(const Vec3f target, const Vec3f up = Vec3f::sUp);

	virtual const Mat4f& GetCameraMatrix(eObjectLayer layer) const { return mCameraMatrix; }

	FX_FORCE_INLINE const Mat4f& GetViewMatrix() const { return mViewMatrix; }
	FX_FORCE_INLINE const Mat4f& GetProjectionMatrix() const { return mProjectionMatrix; }

	FX_FORCE_INLINE float32 GetNearClip() const { return mZNearClip; }
	FX_FORCE_INLINE float32 GetFarClip() const { return mZFarClip; }

	virtual ~BaseCamera() = default;

protected:
	void RebuildViewMatrix();

protected:
	/// The result matrix for the camera.
	Mat4f mCameraMatrix = Mat4f::scIdentity;

	Mat4f mProjectionMatrix = Mat4f::scIdentity;
	Mat4f mViewMatrix = Mat4f::scIdentity;

	Vec3f mPosition = Vec3f::sZero;
	Quat mRotation = Quat::scIdentity;


	/// True if the position or rotation has changed
	bool mbTransformDirty = true;

	bool mbRequireProjectionMatrixUpdate = true;

	float32 mZNearClip = 750.0f;
	float32 mZFarClip = 0.01f;
};


} // namespace fx
