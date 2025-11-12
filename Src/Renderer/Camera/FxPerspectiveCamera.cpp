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

    ProjectionMatrix.Columns[0].X = (Sa);
    ProjectionMatrix.Columns[1].Y = (-Sv);

    ProjectionMatrix.Columns[2].Z = (a);
    ProjectionMatrix.Columns[2].W = (-1);

    ProjectionMatrix.Columns[3].Z = (b);
    ProjectionMatrix.Columns[3].W = (0);

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


void FxPerspectiveCamera::UpdateViewMatrix()
{
    FxVec3f target = Position - Direction;
    ViewMatrix.LookAt(Position, target, FxVec3f::sUp);

    VPMatrix = ViewMatrix * ProjectionMatrix;

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
