#include "RxGlobals.hpp"

#include "RxRenderBackend.hpp"
#include "RxShaderCache.hpp"
#include "RxShadowDirectional.hpp"

RxRenderBackend* gRenderer = nullptr;
RxShadowDirectional* gShadowRenderer = nullptr;
RxShaderCache* gShaderCache = nullptr;

#define DESTROY_GLOBAL(name_)                                                                                          \
    delete name_;                                                                                                      \
    name_ = nullptr

namespace RxGlobals {

void Init()
{
    gRenderer = new RxRenderBackend;
    gShaderCache = new RxShaderCache;
}

void Destroy()
{
    if (gShadowRenderer) {
        DESTROY_GLOBAL(gShadowRenderer);
    }

    DESTROY_GLOBAL(gShaderCache);
    DESTROY_GLOBAL(gRenderer);
}

}; // namespace RxGlobals
