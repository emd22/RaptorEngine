#pragma once

namespace fx::renderer {

class ShadowDirectional;
class RenderBackend;
class ShaderCache;
class DescriptorCache;
class State;

extern RenderBackend* gRenderer;
extern ShadowDirectional* gShadowRenderer;
extern ShaderCache* gShaderCache;
extern DescriptorCache* gDescriptorCache;
extern State* gState;

namespace Globals {

void Init();
void Destroy();

}; // namespace Globals

} // namespace fx::renderer
