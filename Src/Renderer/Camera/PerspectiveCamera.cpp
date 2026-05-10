#include <Renderer/Camera.hpp>

namespace fx {

void PerspectiveCamera::UpdateProjectionMatrix()
{
    ProjectionMatrix.LoadPerspectiveMatrix(mFovRad, mAspectRatio, mNearPlane, mFarPlane);
    mWeaponProjectionMatrix.LoadPerspectiveMatrix(mWeaponFov, mAspectRatio, scWeaponNearPlane, scWeaponFarPlane);

    mbRequireMatrixUpdate = false;
}

void PerspectiveCamera::UpdateCameraMatrix()
{
    mCameraMatrix = ViewMatrix * ProjectionMatrix;
    mWeaponCameraMatrix = ViewMatrix * mWeaponProjectionMatrix;

    InvViewMatrix = ViewMatrix.Inverse();
    InvProjectionMatrix = ProjectionMatrix.Inverse();
}

void PerspectiveCamera::OnWindowResize(const Vec2u& size) 
{ 
    float32 aspect_ratio = static_cast<float32>(size.X) / static_cast<float32>(size.Y); 
    SetAspectRatio(aspect_ratio);
}

} // namespace fx
