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
RenderState* gRenderState = nullptr;
DescriptorCache* gDescriptorCache = nullptr;

#define DESTROY_GLOBAL(name_)                                                                                          \
	delete name_;                                                                                                      \
	name_ = nullptr

namespace Globals {


void Init()
{
	gSamplerCache = new SamplerCache;
	gPipelineCache = new PipelineCache;
	gRenderState = new RenderState;

	gRenderer = new RenderBackend;
	gShaderCache = new ShaderCache;
	gDsLayoutCache = new DsLayoutCache;
	gDescriptorCache = new DescriptorCache;
}

void Destroy()
{
	if (gShadowRenderer) {
		DESTROY_GLOBAL(gShadowRenderer);
	}

	DESTROY_GLOBAL(gSamplerCache);

	DESTROY_GLOBAL(gDescriptorCache);
	DESTROY_GLOBAL(gDsLayoutCache);
	DESTROY_GLOBAL(gPipelineCache);
	DESTROY_GLOBAL(gShaderCache);

	DESTROY_GLOBAL(gRenderState);
	DESTROY_GLOBAL(gRenderer);
}

}; // namespace Globals
} // namespace fx::renderer
