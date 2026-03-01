#pragma once

class RxShadowDirectional;
class RxRenderBackend;

extern RxRenderBackend* gRenderer;
extern RxShadowDirectional* gShadowRenderer;

namespace RxGlobals {
void Init();
void Destroy();
}; // namespace RxGlobals
