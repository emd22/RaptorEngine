#include "State.hpp"

#include "Backend/VertexDescription.hpp"
#include "Globals.hpp"
#include "PipelineCache.hpp"
#include "ShaderCache.hpp"

namespace fx::renderer {

void State::BeginPipeline(ePipelineName pipeline) { mpPipeline = gPipelineCache->Request(pipeline); }

void State::SetShader(eShaderName shader_name, const SizedArray<ShaderMacro>& macros)
{
    Ref<Shader> shader = gShaderCache->Request(shader_name);
    mpVertexShader = shader->GetProgram(eShaderType::Vertex, macros);
    mpPixelShader = shader->GetProgram(eShaderType::Pixel, macros);
}


void State::SetLayout(VkPipelineLayout layout) { mpPipeline->SetLayout(layout); }

void State::BuildPipeline()
{
    SizedArray<Ref<ShaderProgram>> shader_list = { mpVertexShader, mpPixelShader };
    if (!mpVertexShader.IsValid() || !mpPixelShader.IsValid()) {
        LogError("Cannot build pipeline due to invalid shaders");
        return;
    }

    VertexDescription vertex_desc = VertexUtil::BuildDescription(mVertexType);

    // mpPipeline->Create(PipelineNameUtil::GetName(mPipelineName), shader_list, OutputTargets.GetImageViews(),
    //                    BlendAttachments.GetVkAttachments(OutputTargets.Targets.Size), &vertex_desc);
}

void State::SetTargetBlend(uint32 target_index, const BlendAttachment& blend_attachment)
{
    BlendAttachments.AddAttachment(target_index, blend_attachment);
}

void State::SetOutputTargets(const TargetList& targets) { OutputTargets = targets; }

void State::EndPipeline()
{
    BuildPipeline();
    Reset();
}

void State::Reset()
{
    mpPipeline = nullptr;

    OutputTargets.Reset();
    mPushConstants.Clear();
}

} // namespace fx::renderer
