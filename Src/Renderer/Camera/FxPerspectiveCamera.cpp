#include <Renderer/FxCamera.hpp>


void FxPerspectiveCamera::UpdateProjectionMatrix()
{
    ProjectionMatrix.LoadIdentity();

    const float32 Sv = 1.0f / tan(mFovRad * 0.5f);
    const float32 Sa = Sv / mAspectRatio;

    // const float32 plane_diff = mNearPlane - mFarPlane;

    const float32 a = mNearPlane / (mFarPlane - mNearPlane);
    // const float32 a = (mFarPlane + mNearPlane) / plane_diff;
    // const float32 b = (2.0f * mFarPlane * mNearPlane) / plane_diff;
    const float32 b = mFarPlane * a;

    /*
    Column 0    1    2    3
    ------------------------
           Sa,   0,   0,   0,
           0,  -Sv,   0,   0,
           0,    0,   a,   b,
           0,    0,  -1,   0
    */

    ProjectionMatrix.Columns[0].SetX(Sa);
    ProjectionMatrix.Columns[1].SetY(-Sv);

    ProjectionMatrix.Columns[2].SetZ(a);
    ProjectionMatrix.Columns[2].SetW(-1);

    ProjectionMatrix.Columns[3].SetZ(b);
    ProjectionMatrix.Columns[3].SetW(0);

    ProjectionMatrix.Print();
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

void FxPerspectiveCamera::Update()
{
    if (!mRequiresUpdate) {
        return;
    }

    // TODO: add sincos() implementation
    Direction.Set(sin(mAngleX), mAngleY, cos(mAngleX));
    Direction = Direction.Normalize();

    const FxVec3f target = Position + Direction;

    ViewMatrix.LookAt(Position, target, FxVec3f::Up);

    FxMat4f PM = ProjectionMatrix;

    VPMatrix = ViewMatrix * PM;

    InvViewMatrix = ViewMatrix.Inverse();
    InvProjectionMatrix = ProjectionMatrix.Inverse();

    // VPMatrix =  ProjectionMatrix * ViewMatrix;

    mRequiresUpdate = false;
}
