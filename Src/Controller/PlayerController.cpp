#include "PlayerController.hpp"

#include <Renderer/Camera/PerspectiveCamera.hpp>

namespace fx {

FVec3 PlayerController::Update(double delta_time, const FVec3 input, const PerspectiveCamera& camera, bool sprinting)
{
	const FVec3 forward = camera.GetFlatForwardVector();
	const FVec3 right = camera.GetFlatRightVector();

	Accelerate(forward * input.Z + right * input.X, delta_time, sprinting);

	mVelocity.Y = 0.0f;

	return mVelocity;
}

} // namespace fx
