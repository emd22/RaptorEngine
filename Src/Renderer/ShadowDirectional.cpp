#include "ShadowDirectional.hpp"

namespace fx::renderer {

ShadowDirectional::ShadowDirectional()
	: ShadowMapSize(ShadowAtlas::scDirectionalSize, ShadowAtlas::scDirectionalSize)
{
	ShadowCamera.Update();
}

} // namespace fx::renderer
