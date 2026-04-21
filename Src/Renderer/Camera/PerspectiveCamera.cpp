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

} // namespace fx
