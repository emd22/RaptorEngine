#include "RxGlobals.hpp"

#include "RxRenderBackend.hpp"
#include "RxShadowDirectional.hpp"

RxRenderBackend* gRenderer = nullptr;
RxShadowDirectional* gShadowRenderer = nullptr;

#define DESTROY_GLOBAL(name_)                                                                                          \
    delete name_;                                                                                                      \
    name_ = nullptr

namespace RxGlobals {

void Init()
{
    gRenderer = new RxRenderBackend;

    // Add other init functions here
}

void Destroy()
{
    if (gShadowRenderer) {
        DESTROY_GLOBAL(gShadowRenderer);
    }

    FxLogInfo("Destroying renderer globals");
    DESTROY_GLOBAL(gRenderer);

    // Add other destroys here
}

}; // namespace RxGlobals
