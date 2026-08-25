#include "Globals.hpp"

#include "Backend/DescriptorCache.hpp"
#include "Backend/Sampler/SamplerCache.hpp"
#include "GraphicsBackend.hpp"
#include "PSOBuild.hpp"
#include "PipelineCache.hpp"
#include "ShaderCache.hpp"
#include "ShadowDirectional.hpp"

namespace fx::renderer {

GraphicsBackend* gGraphics = nullptr;
ShadowDirectional* gShadowRenderer = nullptr;
ShaderCache* gShaderCache = nullptr;
DsLayoutCache* gDsLayoutCache = nullptr;
PipelineCache* gPipelineCache = nullptr;
SamplerCache* gSamplerCache = nullptr;
PSOBuild* gPSOBuild = nullptr;
DescriptorCache* gDescriptorCache = nullptr;

#define DESTROY_GLOBAL(name_)                                                                                          \
	delete name_;                                                                                                      \
	name_ = nullptr

namespace Globals {


void Init()
{
	gSamplerCache = new SamplerCache;
	gPipelineCache = new PipelineCache;
	gPSOBuild = new PSOBuild;

	gGraphics = new GraphicsBackend;
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

	DESTROY_GLOBAL(gPSOBuild);
	DESTROY_GLOBAL(gGraphics);
}

}; // namespace Globals
} // namespace fx::renderer
