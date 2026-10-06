
#include "Pipeline.hpp"

#include "Commands.hpp"
#include "DescriptorCache.hpp"
#include "Shader.hpp"
#include "VertexDescription.hpp"

#include <Core/Assert.hpp>
#include <Core/Defines.hpp>
#include <Core/StackArray.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/TiledForwardRenderer.hpp>

FX_SET_MODULE_NAME("Pipeline")

namespace fx::renderer {

/////////////////////////////////////
// Pipeline
/////////////////////////////////////

void Pipeline::Bind(const CommandBuffer& cmd) const
{
	rx_gpu_cmd_bind_pipeline_cached(gGraphics->GetDevice()->GetRustDevice(), cmd.Cmd, &cmd.BoundPipeline,
									GetBindPoint(), pipeline, IsCompute(), default_cull_mode);
}

void Pipeline::SetDoubleSided(const CommandBuffer& cmd, bool double_sided) const
{
	rx_gpu_cmd_set_double_sided(gGraphics->GetDevice()->GetRustDevice(), cmd.Cmd, IsCompute(), default_cull_mode,
								double_sided);
}

} // namespace fx::renderer
