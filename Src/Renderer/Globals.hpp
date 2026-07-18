#pragma once

namespace fx::renderer {

class RenderBackend;
extern RenderBackend* gRenderer;

class ShadowDirectional;
extern ShadowDirectional* gShadowRenderer;

class ShaderCache;
extern ShaderCache* gShaderCache;

class PipelineCache;
extern PipelineCache* gPipelineCache;

class SamplerCache;
extern SamplerCache* gSamplerCache;

class DsLayoutCache;
extern DsLayoutCache* gDsLayoutCache;

class DescriptorCache;
extern DescriptorCache* gDescriptorCache;

class PSOBuild;
extern PSOBuild* gPSOBuild;

namespace Globals {

void Init();
void Destroy();

}; // namespace Globals

} // namespace fx::renderer
