#include <Core/Panic.hpp>
#include <Renderer/Camera.hpp>

namespace fx {

void OrthoCamera::UpdateProjectionMatrix()
{
    ProjectionMatrix.LoadOrthographicMatrix(mWidth, mHeight, mNearPlane, mFarPlane);

    mbRequireMatrixUpdate = false;
}

void OrthoCamera::UpdateCameraMatrix()
{
    mCameraMatrix = ViewMatrix * ProjectionMatrix;

    InvViewMatrix = ViewMatrix.Inverse();
    InvProjectionMatrix = ProjectionMatrix.Inverse();
}

void OrthoCamera::ResolveViewToTexels(float32 texture_res)
{
    Assert(texture_res > 0.0f);

    const float32 texel_size = mWidth / texture_res;
    const float32 texel_size_recip = 1.0 / texel_size;

    float32 snapped_x = floor(Position.X * texel_size_recip) * texel_size;
    float32 snapped_y = floor(Position.Y * texel_size_recip) * texel_size;

    Position.X = snapped_x;
    Position.Y = snapped_y;
}

void OrthoCamera::OnWindowResize(const Vec2u& size) 
{ 
    
}

} // namespace fx
