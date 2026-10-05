
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
	const VkPipeline pipeline = Get();

	if (pipeline == cmd.pBoundPipeline) {
		return;
	}

	RxGpuDevice* device = gGraphics->GetDevice()->GetRustDevice();

	rx_gpu_cmd_bind_pipeline(device, cmd.Cmd, GetBindPoint(), RxRaw(pipeline));

	if (!IsCompute()) {
		rx_gpu_cmd_set_cull_mode(device, cmd.Cmd, GetDefaultCullMode());
	}

	cmd.pBoundPipeline = pipeline;
}

void Pipeline::SetDoubleSided(const CommandBuffer& cmd, bool double_sided) const
{
	if (!IsCompute()) {
		rx_gpu_cmd_set_cull_mode(gGraphics->GetDevice()->GetRustDevice(), cmd.Cmd,
								 double_sided ? VK_CULL_MODE_NONE : GetDefaultCullMode());
	}
}


} // namespace fx::renderer
