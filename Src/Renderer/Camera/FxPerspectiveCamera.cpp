#include <Renderer/FxCamera.hpp>

void FxPerspectiveCamera::UpdateProjectionMatrix()
{
    ProjectionMatrix.LoadPerspectiveMatrix(mFovRad, mAspectRatio, mNearPlane, mFarPlane);
    WeaponProjectionMatrix.LoadPerspectiveMatrix(mWeaponFov, mAspectRatio, mNearPlane, mFarPlane);
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

    float32 s_anglex, c_anglex;
    float32 s_angley, c_angley;

    FxMath::SinCos(mAngleX, &s_anglex, &c_anglex);
    FxMath::SinCos(mAngleY, &s_angley, &c_angley);

    Direction.Set(c_angley * s_anglex, s_angley, c_angley * c_anglex);
    Direction.NormalizeIP();

    UpdateViewMatrix();

    mbUpdateTransform = false;
}
