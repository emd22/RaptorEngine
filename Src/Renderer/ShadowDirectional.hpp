#pragma once

#include <Math/Vec2.hpp>
#include <Renderer/Camera/OrthoCamera.hpp>
#include <Renderer/ShadowAtlas.hpp>

namespace fx::renderer {

/**
 * @brief Shadow camera for the directional light. Its shadow map is the directional region of the shadow atlas, see
 * ShadowAtlas::GetDirectionalRegion().
 */
class ShadowDirectional
{
public:
	/// How far back along the sun direction the shadow camera sits from what it is centered on
	static constexpr float32 scCameraDistance = 25.0f;

	static constexpr float32 scFarCameraDistance = 150.0f;
	static constexpr float32 scFarBounds = 200.0f;

public:
	ShadowDirectional();

	void PlaceCamera(OrthoCamera& camera, const FVec3 target, const FVec3 sun_direction) const;
	void PlaceFarCamera(const FVec3 target, const FVec3 sun_direction);

	static bool IsWellCovered(const OrthoCamera& camera, const FVec3 position, float32 edge_margin);

	~ShadowDirectional() = default;

private:
	static void PlaceOrthoCamera(OrthoCamera& camera, const FVec3 target, const FVec3 sun_direction, float32 distance,
								 float32 resolution);

public:
	OrthoCamera ShadowCamera;
	OrthoCamera FarShadowCamera;

	/// Size of the directional region of the shadow atlas
	UVec2 ShadowMapSize = UVec2::scZero;
};

} // namespace fx::renderer
