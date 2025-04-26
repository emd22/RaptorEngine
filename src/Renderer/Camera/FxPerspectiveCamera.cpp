#include <Renderer/FxCamera.hpp>


void FxPerspectiveCamera::UpdateProjectionMatrix()
{
    const float32 Sv = 1.0 / tan(mFovRad * 0.5f);
    const float32 Sa = Sv / mAspectRatio;

    const float32 plane_diff = mFarPlane - mNearPlane;

    const float a = -(mFarPlane + mNearPlane) / plane_diff;
    const float b = -(2.0 * mFarPlane * mNearPlane) / plane_diff;

    /*
    Column 0    1    2    3
    ------------------------
           Sa,  0,   0,   0,
           0,   Sv,  0,   0,
           0,   0,   a,   b,
           0,   0,  -1,   0
    */

    ProjectionMatrix.Data[0].SetX(Sa);
    ProjectionMatrix.Data[1].SetY(Sv);

    ProjectionMatrix.Data[2].SetZ(a);
    ProjectionMatrix.Data[2].SetW(-1);

    ProjectionMatrix.Data[3].SetZ(b);
}

void FxPerspectiveCamera::Update()
{
    if (!mRequiresUpdate) {
        return;
    }

    // TODO: add sincos() implementation
    Direction.Set(sin(mAngleX), mAngleY, cos(mAngleX));
    Direction = Direction.Normalize();

    const Vec3f target = Position - Direction;

    ViewMatrix.LookAt(Position, target, Vec3f(0, 1, 0));

    mRequiresUpdate = false;
}
