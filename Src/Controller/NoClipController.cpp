#include "NoClipController.hpp"

#include <Renderer/Camera/PerspectiveCamera.hpp>

namespace fx {

Vec3f NoClipController::Update(float64 delta_time, const Vec3f input, const PerspectiveCamera& camera, bool sprinting)
{
	const Vec3f forward = camera.GetForwardVector();
	const Vec3f right = camera.GetFlatRightVector();

	Accelerate(forward * input.Z + right * input.X + Vec3f::sUp * input.Y, delta_time, sprinting);

	return mVelocity;
}

} // namespace fx
