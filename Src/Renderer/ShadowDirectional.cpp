#include "ShadowDirectional.hpp"

namespace fx::renderer {

ShadowDirectional::ShadowDirectional()
	: ShadowMapSize(ShadowAtlas::scDirectionalSize, ShadowAtlas::scDirectionalSize)
{
	ShadowCamera.Update();
}

void ShadowDirectional::PlaceCamera(OrthoCamera& camera, const Vec3f& target, const Vec3f& sun_direction) const
{
	camera.Position = target + (sun_direction * scCameraDistance);

	Vec3f snapped_target = target;
	camera.ResolveViewToTexels(camera.Position, snapped_target, Vec3f::sUp, static_cast<float32>(ShadowMapSize.X));

	camera.ViewMatrix.LookAt(camera.Position, snapped_target, Vec3f::sUp);
	camera.UpdateCameraMatrix();
	camera.mbRequireMatrixUpdate = false;
}

bool ShadowDirectional::IsWellCovered(const OrthoCamera& camera, const Vec3f& position, float32 edge_margin)
{
	const float32* m = camera.GetCameraMatrix(eObjectLayer::WorldLayer).RawData;

	// Row vector convention (v * M). The projection is orthographic, so w is 1 and this is already NDC.
	const float32 x = position.X * m[0] + position.Y * m[4] + position.Z * m[8] + m[12];
	const float32 y = position.X * m[1] + position.Y * m[5] + position.Z * m[9] + m[13];
	const float32 z = position.X * m[2] + position.Y * m[6] + position.Z * m[10] + m[14];

	const float32 limit = 1.0f - edge_margin;

	return (fabsf(x) <= limit) && (fabsf(y) <= limit) && (z >= 0.0f) && (z <= 1.0f);
}

} // namespace fx::renderer
