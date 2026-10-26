/*
 * File:        FPSCamera.hpp
 * Author:      emd22
 * Created:     26/10/2026
 * Description: Similar to perspective camera, but with FPS specific features and separate weapon matrices.
 */


#pragma once

#include "PerspectiveCamera.hpp"

#include <Math/MathConsts.hpp>
#include <Object/ObjectLayer.hpp>

namespace fx {


struct ViewAngles
{
	static constexpr float32 scPitchLimit = static_cast<float32>(FX_HALF_PI) - 0.01f;

	void Rotate(float32 yaw_delta, float32 pitch_delta)
	{
		Yaw = std::fmod(Yaw + yaw_delta, static_cast<float32>(FX_2PI));
		Pitch = MathUtil::Clamp(Pitch + pitch_delta, -scPitchLimit, scPitchLimit);
	}

	void SetDirection(const Vec3f direction)
	{
		const Vec3f unit = direction.Normalize();

		Yaw = std::atan2(unit.X, unit.Z);
		Pitch = std::asin(MathUtil::Clamp(unit.Y, -1.0f, 1.0f));
	}

	Quat ToQuat() const { return Quat::FromEulerAngles(Vec3f(-Pitch, Yaw, 0.0f)); }

	float32 Yaw = 0.0f;
	float32 Pitch = 0.0f;
};

class FPSCamera final : public PerspectiveCamera2
{
public:
	FPSCamera() = default;

	bool Update() override;

	const Mat4f& GetCameraMatrix(eObjectLayer layer) const override
	{
		if (layer == eObjectLayer::PlayerLayer) {
			return mWeaponCameraMatrix;
		}

		return mCameraMatrix;
	}

	~FPSCamera() override = default;

public:
private:
	Mat4f mWeaponCameraMatrix = Mat4f::scIdentity;
	Mat4f mWeaponProjectionMatrix = Mat4f::scIdentity;

	float32 mWeaponFovY = MathUtil::DegreesToRadians(50.0f);
};


} // namespace fx
