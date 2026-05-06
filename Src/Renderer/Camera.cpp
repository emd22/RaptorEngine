#include "Camera.hpp"

namespace fx {

void Camera::Update()
{
    if (mbRequireMatrixUpdate) {
        UpdateProjectionMatrix();
    }

    if (!mbUpdateTransform) {
        return;
    }

    float32 s_anglex, c_anglex;
    float32 s_angley, c_angley;

    MathUtil::SinCos(mAngleX, &s_anglex, &c_anglex);
    MathUtil::SinCos(mAngleY, &s_angley, &c_angley);

    Direction.Set(c_angley * s_anglex, s_angley, c_angley * c_anglex);
    Direction.NormalizeIP();

    UpdateViewMatrix();

    mbUpdateTransform = false;
}


void Camera::UpdateViewMatrix()
{
    if (!bLookatTarget) {
        Target = Position + Direction;
    }

    ViewMatrix.LookAt(Position, Target, Vec3f::sUp);
    UpdateCameraMatrix();
}

} // namespace fx
