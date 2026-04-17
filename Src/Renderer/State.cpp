#include "State.hpp"

#include "Backend/RenderPass.hpp"
#include "Backend/Shader.hpp"
#include "PipelineBuilder.hpp"

#include <Core/Slice.hpp>

namespace fx::renderer {

State::State() {}

void State::Pipeline(Pipeline* pipeline) { mpPipeline = pipeline; }

void State::BufferOffset(ShaderType shader_type, uint32 buffer_offset)
{
    ShaderBindOptions& dyn = mBindOptions[static_cast<uint32>(shader_type)];
    dyn.bUseOffset = true;
    dyn.BufferOffset = buffer_offset;
}

void State::RenderPass(RenderPass* rp) { mpRenderPass = rp; }

void State::Apply(const CommandBuffer& cmd)
{
    // mpPipeline->Bind(cmd);

    mpPipeline->VertexShader->Bind(cmd, *mpPipeline, mBindOptions[static_cast<uint32>(ShaderType::Vertex)]);
    mpPipeline->FragmentShader->Bind(cmd, *mpPipeline, mBindOptions[static_cast<uint32>(ShaderType::Fragment)]);
}

void State::Reset()
{
    memset(mBindOptions.pData, 0, mBindOptions.GetSizeInBytes());
    mpPipeline = nullptr;
    mpRenderPass = nullptr;
}


State::~State() {}

} // namespace fx::renderer
