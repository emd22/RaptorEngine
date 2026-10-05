#include "Globals.hpp"

#include "Backend/DescriptorCache.hpp"
#include "Backend/Sampler/SamplerCache.hpp"
#include "DebugDraw.hpp"
#include "GraphicsBackend.hpp"
#include "PSOBuild.hpp"
#include "PipelineCache.hpp"
#include "ShaderLibrary.hpp"

#include <Asset/AxPaths.hpp>
#include "ShadowAtlas.hpp"
#include "ShadowDirectional.hpp"
#include "TextRenderer.hpp"

namespace fx::renderer {

GraphicsBackend* gGraphics = nullptr;
ShadowDirectional* gShadowRenderer = nullptr;
ShadowAtlas* gShadowAtlas = nullptr;
ShaderLibrary* gShaderLibrary = nullptr;
DsLayoutCache* gDsLayoutCache = nullptr;
PipelineCache* gPipelineCache = nullptr;
SamplerCache* gSamplerCache = nullptr;
PSOBuild* gPSOBuild = nullptr;
DescriptorCache* gDescriptorCache = nullptr;
TextRenderer* gTextRenderer = nullptr;
DebugDraw* gDebugDraw = nullptr;

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
	gShaderLibrary = new ShaderLibrary(AssetPath(eAxPathQuery::Shaders));
	gDsLayoutCache = new DsLayoutCache;
	gDescriptorCache = new DescriptorCache;
	gTextRenderer = new TextRenderer;
	gDebugDraw = new DebugDraw;
}

void Destroy()
{
	if (gShadowRenderer) {
		DESTROY_GLOBAL(gShadowRenderer);
	}

	if (gShadowAtlas) {
		DESTROY_GLOBAL(gShadowAtlas);
	}

	DESTROY_GLOBAL(gSamplerCache);

	DESTROY_GLOBAL(gDescriptorCache);
	DESTROY_GLOBAL(gDsLayoutCache);
	DESTROY_GLOBAL(gPipelineCache);
	DESTROY_GLOBAL(gShaderLibrary);
	DESTROY_GLOBAL(gTextRenderer);
	DESTROY_GLOBAL(gDebugDraw);

	DESTROY_GLOBAL(gPSOBuild);
	DESTROY_GLOBAL(gGraphics);
}
}; // namespace Globals
} // namespace fx::renderer
