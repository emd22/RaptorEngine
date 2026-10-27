#include "FPSCamera.hpp"

namespace fx {


bool FPSCamera::Update()
{
	// If there is a projection update required, build the weapon projection matrix first
	if (mbRequireProjectionMatrixUpdate) {
		static constexpr float32 scWeaponDepthScale = 0.01f;

		mWeaponProjectionMatrix.LoadPerspectiveMatrix(mWeaponFovY, mAspectRatio, 20.0f, 0.01f);

		// The weapon/viewmodel is on its own depth scale
		mWeaponProjectionMatrix.Rows[2].Z *= scWeaponDepthScale;
		mWeaponProjectionMatrix.Rows[3].Z *= scWeaponDepthScale;
	}

	const bool did_update = this->PerspectiveCamera::Update();

	if (did_update) {
		mWeaponCameraMatrix = mViewMatrix * mWeaponProjectionMatrix;
	}

	return did_update;
}

} // namespace fx
