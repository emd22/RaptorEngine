#pragma once

class RxShadowDirectional;
class RxRenderBackend;
class RxShaderCache;
class RxDescriptorCache;

extern RxRenderBackend* gRenderer;
extern RxShadowDirectional* gShadowRenderer;
extern RxShaderCache* gShaderCache;
extern RxDescriptorCache* gDescriptorCache;

namespace RxGlobals {
void Init();
void Destroy();
}; // namespace RxGlobals
