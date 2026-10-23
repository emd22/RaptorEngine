#include "BaseController.hpp"

namespace fx {

float32 BaseController::GetMaxSpeed(bool sprinting) const
{
	float32 max_speed = Speed;

	if (sprinting) {
		max_speed = SprintSpeed;
	}

	return max_speed * SpeedMultiplier;
}

void BaseController::Accelerate(const Vec3f direction, double delta_time, bool sprinting)
{
	mGoal = direction;

	if (mGoal.Length() > 1e-3) {
		mGoal.NormalizeIP();
	}

	mVelocity.SmoothInterpolate(mGoal * GetMaxSpeed(sprinting), MovementLerpSpeed, static_cast<float32>(delta_time));
}

} // namespace fx
