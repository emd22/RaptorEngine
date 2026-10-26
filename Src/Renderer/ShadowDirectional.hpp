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

	/**
	 * @brief Centers `camera` on `target`, looking down along the sun. `sun_direction` points towards the sun and must
	 * be normalized.
	 */
	void PlaceCamera(OrthoCamera2& camera, const Vec3f target, const Vec3f sun_direction) const;

	void PlaceFarCamera(const Vec3f target, const Vec3f sun_direction);

	/**
	 * @brief Checks that `position` is inside of `camera`'s shadow map and away from its edges, so that its
	 * surroundings are shadowed too.
	 *
	 * @param edge_margin Fraction of the shadow map's half-width that has to be left between `position` and every
	 * edge, 0.5 keeps it within the middle half.
	 */
	static bool IsWellCovered(const OrthoCamera2& camera, const Vec3f position, float32 edge_margin);

	~ShadowDirectional() = default;

private:
	static void PlaceOrthoCamera(OrthoCamera2& camera, const Vec3f target, const Vec3f sun_direction, float32 distance,
								 float32 resolution);

public:
	OrthoCamera2 ShadowCamera;
	OrthoCamera2 FarShadowCamera;

	/// Size of the directional region of the shadow atlas
	Vec2u ShadowMapSize = Vec2u::scZero;
};

} // namespace fx::renderer
