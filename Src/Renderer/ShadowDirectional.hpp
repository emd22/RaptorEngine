#pragma once

#include <Math/Vec2.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/ShadowAtlas.hpp>

namespace fx::renderer {

/**
 * @brief Shadow camera for the directional light. Its shadow map is the directional region of the shadow atlas, see
 * ShadowAtlas::GetDirectionalRegion().
 */
class ShadowDirectional
{
public:
	ShadowDirectional();

	~ShadowDirectional() = default;

public:
	OrthoCamera ShadowCamera;

	/// Size of the directional region of the shadow atlas
	Vec2u ShadowMapSize = Vec2u::sZero;
};

} // namespace fx::renderer
