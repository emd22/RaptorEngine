#include "SwimController.hpp"

#include <Renderer/Camera/PerspectiveCamera.hpp>

namespace fx {

SwimController::SwimController() { this->GravityMultiplier = 0.15f; }

FVec3 SwimController::Update(float64 delta_time, const FVec3 input, const PerspectiveCamera& camera, bool sprinting)
{
	const FVec3 forward = camera.GetForwardVector();
	const FVec3 right = camera.GetFlatRightVector();

	Accelerate(forward * input.Z + right * input.X + FVec3::sUp * input.Y, delta_time, sprinting);

	return mVelocity;
}

} // namespace fx
