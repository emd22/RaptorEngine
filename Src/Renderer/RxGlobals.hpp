#pragma once

class RxShadowDirectional;
class RxRenderBackend;
class RxShaderCache;

extern RxRenderBackend* gRenderer;
extern RxShadowDirectional* gShadowRenderer;
extern RxShaderCache* gShaderCache;

namespace RxGlobals {
void Init();
void Destroy();
}; // namespace RxGlobals
