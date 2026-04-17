#include "Globals.hpp"

#include "Backend/DescriptorCache.hpp"
#include "RenderBackend.hpp"
#include "ShaderCache.hpp"
#include "ShadowDirectional.hpp"
#include "State.hpp"

namespace fx::renderer {

RenderBackend* gRenderer = nullptr;
ShadowDirectional* gShadowRenderer = nullptr;
ShaderCache* gShaderCache = nullptr;
DescriptorCache* gDescriptorCache = nullptr;
// State* gState = nullptr;

#define DESTROY_GLOBAL(name_)                                                                                          \
    delete name_;                                                                                                      \
    name_ = nullptr

namespace Globals {


void Init()
{
    gRenderer = new RenderBackend;
    gShaderCache = new ShaderCache;
    gDescriptorCache = new DescriptorCache;
    // gState = new State;
}

void Destroy()
{
    if (gShadowRenderer) {
        DESTROY_GLOBAL(gShadowRenderer);
    }

    // DESTROY_GLOBAL(gState);
    DESTROY_GLOBAL(gDescriptorCache);
    DESTROY_GLOBAL(gShaderCache);
    DESTROY_GLOBAL(gRenderer);
}

}; // namespace Globals
} // namespace fx::renderer
