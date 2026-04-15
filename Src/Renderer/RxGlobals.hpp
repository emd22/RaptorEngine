#pragma once

namespace fx {
namespace renderer {

class RxShadowDirectional;
class RxRenderBackend;
class RxShaderCache;
class RxDescriptorCache;
class RxState;

extern RxRenderBackend* gRenderer;
extern RxShadowDirectional* gShadowRenderer;
extern RxShaderCache* gShaderCache;
extern RxDescriptorCache* gDescriptorCache;
extern RxState* gState;

} // namespace renderer

namespace RxGlobals {

void Init();
void Destroy();

}; // namespace RxGlobals

} // namespace fx
