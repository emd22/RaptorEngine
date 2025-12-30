#include <Renderer/FxCamera.hpp>

void FxOrthoCamera::UpdateProjectionMatrix()
{
    ProjectionMatrix.LoadOrthographicMatrix(mLeft, mRight, mBottom, mTop, mNearPlane, mFarPlane);
}

void FxOrthoCamera::UpdateCameraMatrix()
{
    mCameraMatrix = ViewMatrix * ProjectionMatrix;

    InvViewMatrix = ViewMatrix.Inverse();
    InvProjectionMatrix = ProjectionMatrix.Inverse();
}
