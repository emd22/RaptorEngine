#include "PlayerController.hpp"

#include <Math/MathUtil.hpp>
#include <Renderer/Camera.hpp>

namespace fx {

Vec3f PlayerController::Update(double delta_time, const Vec3f input, const PerspectiveCamera& camera, bool sprinting)
{
	float32 s_anglex, c_anglex;
	MathUtil::SinCos(camera.mAngleX, &s_anglex, &c_anglex);

	const Vec3f forward(s_anglex, 0.0f, c_anglex);
	const Vec3f right(c_anglex, 0.0f, -s_anglex);

	Accelerate(forward * input.Z + right * input.X, delta_time, sprinting);

	mVelocity.Y = 0.0f;

	return mVelocity;
}

} // namespace fx
