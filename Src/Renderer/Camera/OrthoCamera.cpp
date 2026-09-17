#include <Core/Assert.hpp>
#include <Renderer/Camera.hpp>

namespace fx {

void OrthoCamera::UpdateProjectionMatrix()
{
	ProjectionMatrix.LoadOrthographicMatrix(mWidth, mHeight, mZNearClip, mZFarClip);

	mbRequireMatrixUpdate = false;
}

void OrthoCamera::UpdateCameraMatrix()
{
	mCameraMatrix = ViewMatrix * ProjectionMatrix;

	InvViewMatrix = ViewMatrix.Inverse();
	InvProjectionMatrix = ProjectionMatrix.Inverse();
}

void OrthoCamera::ResolveViewToTexels(Vec3f& eye, Vec3f& target, const Vec3f& world_up, float32 texture_res)
{
	Assert(texture_res > 0.0f);

	const Vec3f forward = (target - eye).Normalize();
	const Vec3f right = world_up.Cross(forward).Normalize();
	const Vec3f up = forward.Cross(right);

	const float32 texel_size = mWidth / texture_res;
	const float32 texel_size_recip = 1.0f / texel_size;

	const float32 view_x = eye.Dot(right);
	const float32 view_y = eye.Dot(up);

	const float32 snapped_x = floor(view_x * texel_size_recip) * texel_size;
	const float32 snapped_y = floor(view_y * texel_size_recip) * texel_size;

	// Shift both eye and target by the same offset so the view direction is unchanged -- only the
	// position within the texel grid moves.
	const Vec3f offset = right * (snapped_x - view_x) + up * (snapped_y - view_y);

	eye += offset;
	target += offset;
}

void OrthoCamera::OnWindowResize(const Vec2u& size) {}

} // namespace fx
