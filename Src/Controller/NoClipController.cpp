#include "NoClipController.hpp"

#include <Math/MathUtil.hpp>
#include <Renderer/Camera.hpp>

namespace fx {

Vec3f NoClipController::Update(float64 delta_time, const Vec3f input, const PerspectiveCamera& camera, bool sprinting)
{
	float32 s_anglex, c_anglex;
	MathUtil::SinCos(camera.mAngleX, &s_anglex, &c_anglex);

	float32 s_angley, c_angley;
	MathUtil::SinCos(camera.mAngleY, &s_angley, &c_angley);

	const Vec3f forward(s_anglex * c_angley, s_angley, c_anglex * c_angley);
	const Vec3f right(c_anglex, 0.0f, -s_anglex);

	Accelerate(forward * input.Z + right * input.X + Vec3f::sUp * input.Y, delta_time, sprinting);

	return mVelocity;
}

} // namespace fx
