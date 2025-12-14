#include <Renderer/FxCamera.hpp>

void FxPerspectiveCamera::UpdateProjectionMatrix()
{
    ProjectionMatrix.LoadProjectionMatrix(mFovRad, mAspectRatio, mNearPlane, mFarPlane);
    WeaponProjectionMatrix.LoadProjectionMatrix(mWeaponFov, mAspectRatio, mNearPlane, mFarPlane);
}

/*
 mat4 MakeProjectionMatrix(float fovy_rads, float s, float near, float far)
 {
     float g = 1.0f / tan(fovy_rads * 0.5);
     float k = far / (far - near);

     return mat4(g / s,  0.0f,   0.0f,   0.0f,
                  0.0f,  g,      0.0f,   0.0f,
                  0.0f,  0.0f,   k,      -near * k,
                  0.0f,  0.0f,   1.0f,   0.0f);
 }
 *
 */


void FxPerspectiveCamera::UpdateViewMatrix()
{
    FxVec3f target = Position - Direction;
    ViewMatrix.LookAt(Position, target, FxVec3f::sUp);

    VPMatrix = ViewMatrix * ProjectionMatrix;
    WeaponVPMatrix = ViewMatrix * WeaponProjectionMatrix;

    InvViewMatrix = ViewMatrix.Inverse();
    InvProjectionMatrix = ProjectionMatrix.Inverse();
}

void FxPerspectiveCamera::Update()
{
    if (!mbUpdateTransform) {
        return;
    }

    const float32 c_angley = std::cos(mAngleY);
    const float32 c_anglex = std::cos(mAngleX);
    const float32 s_angley = std::sin(mAngleY);
    const float32 s_anglex = std::sin(mAngleX);

    Direction.Set(c_angley * s_anglex, s_angley, c_angley * c_anglex);
    Direction.NormalizeIP();

    UpdateViewMatrix();

    mbUpdateTransform = false;
}
