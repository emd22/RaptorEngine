#include "PlayerController.hpp"

#include <Renderer/Camera/PerspectiveCamera.hpp>

namespace fx {

Vec3f PlayerController::Update(double delta_time, const Vec3f input, const PerspectiveCamera2& camera, bool sprinting)
{
	const Vec3f forward = camera.GetFlatForwardVector();
	const Vec3f right = camera.GetFlatRightVector();

	Accelerate(forward * input.Z + right * input.X, delta_time, sprinting);

	mVelocity.Y = 0.0f;

	return mVelocity;
}

} // namespace fx
