#include "Globals.hpp"

#include "Backend/DescriptorCache.hpp"
#include "Backend/Sampler/SamplerCache.hpp"
#include "PipelineCache.hpp"
#include "RenderBackend.hpp"
#include "ShaderCache.hpp"
#include "ShadowDirectional.hpp"
#include "State.hpp"

namespace fx::renderer {

RenderBackend* gRenderer = nullptr;
ShadowDirectional* gShadowRenderer = nullptr;
ShaderCache* gShaderCache = nullptr;
DsLayoutCache* gDsLayoutCache = nullptr;
PipelineCache* gPipelineCache = nullptr;
SamplerCache* gSamplerCache = nullptr;
State* gState = nullptr;

#define DESTROY_GLOBAL(name_)                                                                                          \
    delete name_;                                                                                                      \
    name_ = nullptr

namespace Globals {


void Init()
{
    gSamplerCache = new SamplerCache;
    gPipelineCache = new PipelineCache;
    gState = new State;

    gRenderer = new RenderBackend;
    gShaderCache = new ShaderCache;
    gDsLayoutCache = new DsLayoutCache;
}

void Destroy()
{
    if (gShadowRenderer) {
        DESTROY_GLOBAL(gShadowRenderer);
    }

    DESTROY_GLOBAL(gSamplerCache);

    // DESTROY_GLOBAL(gState);
    DESTROY_GLOBAL(gDsLayoutCache);
    DESTROY_GLOBAL(gPipelineCache);
    DESTROY_GLOBAL(gShaderCache);

    DESTROY_GLOBAL(gState);
    DESTROY_GLOBAL(gRenderer);
}

}; // namespace Globals
} // namespace fx::renderer
