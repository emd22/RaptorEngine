#include "State.hpp"

#include "Backend/DescriptorCache.hpp"
#include "Backend/RenderPass.hpp"
#include "Backend/VertexDescription.hpp"
#include "Globals.hpp"
#include "PipelineCache.hpp"
#include "RenderStage.hpp"
#include "ShaderCache.hpp"

namespace fx::renderer {

void State::BeginPipeline(ePipelineName name)
{
    mPipelineName = name;
    mpPipeline = &gPipelineCache->Request(name);

    if (!mDescriptors.IsInited()) {
        mDescriptors.InitCapacity(10);
    }
}

void State::SetShader(eShaderName shader_name, const SizedArray<ShaderMacro>& macros)
{
    Ref<Shader> shader = gShaderCache->Request(shader_name);
    mpVertexShader = shader->GetProgram(eShaderType::Vertex, macros);
    mpPixelShader = shader->GetProgram(eShaderType::Pixel, macros);
}

void State::UseRenderStage(RenderStage& stage)
{
    SetOutputTargets(&stage.GetTargets());
    SetRenderPass(&stage.GetRenderPass());
}

void State::SetLayout(const PipelineLayout& layout) { mpPipeline->SetLayout(layout); }
void State::SetLayout(ePipelineName name) { mpPipeline->SetLayout(gPipelineCache->Request(name).Layout); }

void State::BuildPipeline()
{
    if (!mpPipeline->HasLayout()) {
        BuildLayout();
    }

    if (!mpVertexShader.IsValid() || !mpPixelShader.IsValid()) {
        LogError("Invalid shaders provided");
        return;
    }

    if (mpRenderPass == nullptr) {
        LogError("No valid renderpass provided");
        return;
    }

    SizedArray<Ref<ShaderProgram>> shader_list = { mpVertexShader, mpPixelShader };

    VertexDescription* vertex_ptr = nullptr;
    VertexDescription vertex_desc = VertexUtil::BuildDescription(mVertexType);

    // If there is no `NoVertices` flag set, use the built vertex description.
    if ((mFlags & eStateFlags::NoVertices) == 0) {
        vertex_ptr = &vertex_desc;
    }

    // Since the blend attachments apply _only_ to colour targets, we want to get the amount of non-depth targets.
    SizedArray<Target> color_only_targets = pOutputTargets->GetTargetByType(eImageAspectFlag::Color);
    SizedArray<VkPipelineColorBlendAttachmentState> blend_attachments = BlendAttachments.GetVkAttachments(
        color_only_targets.Size);

    LogInfo("!! Built pipeline {}", PipelineNameUtil::GetName(mPipelineName));

    mpPipeline->Create(PipelineNameUtil::GetName(mPipelineName), shader_list, pOutputTargets->GetDescriptions(),
                       blend_attachments, vertex_ptr, *mpRenderPass, mProperties);
}

void State::SetTargetBlend(uint32 target_index, const BlendAttachment& blend_attachment)
{
    BlendAttachments.AddAttachment(target_index, blend_attachment);
}


void State::SetPushConstants(eShaderType type, uint32 pc_size)
{
    PushConstants* pc = mPushConstants.Insert();

    pc->ShaderTypes = type;
    pc->Size = pc_size;
}


PipelineLayout State::BuildLayout()
{
    mpPipeline->Layout = PipelineLayout(Slice(mPushConstants), Slice(mDescriptors));
    return mpPipeline->Layout;
}

void State::AddDescriptorsFromShaderReflection()
{
    SizedArray<ShaderReflectionEntry>& refl = mpVertexShader->Reflection;

    uint32 min_set = 10;
    uint32 max_set = 0;

    for (const ShaderReflectionEntry& entry : refl) {
        if (entry.Set > max_set) {
            max_set = entry.Set;
        }
        else if (entry.Set < min_set) {
            min_set = entry.Set;
        }
    }

    for (int32 i = min_set; i < max_set; i++) {
        // VkDescriptorSetLayout ds_layout = gDsLayoutCache->Request();
    }
}

void State::AddDescriptor(VkDescriptorSetLayout layout) { mDescriptors.Insert(layout); }
void State::SetRenderPass(RenderPass* rp) { mpRenderPass = rp; }
void State::SetOutputTargets(TargetList* targets) { pOutputTargets = targets; }

void State::EndPipeline()
{
    BuildPipeline();
    Reset();
}

void State::Reset()
{
    mpPipeline = nullptr;
    mpRenderPass = nullptr;

    mPushConstants.Clear();
    mDescriptors.Clear();

    BlendAttachments.Clear();

    memset(mPushConstants.pData, 0, mPushConstants.GetSizeInBytes());

    mProperties = PipelineProperties {};
    mFlags = eStateFlags::None;
}

} // namespace fx::renderer
