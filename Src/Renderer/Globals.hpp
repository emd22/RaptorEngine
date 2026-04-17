#pragma once

namespace fx {
namespace renderer {

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

} // namespace renderer

namespace Globals {

void Init();
void Destroy();

}; // namespace Globals

} // namespace fx
