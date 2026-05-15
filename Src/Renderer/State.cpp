#include "State.hpp"

#include "Backend/DescriptorCache.hpp"
#include "Backend/RenderPass.hpp"
#include "Backend/VertexDescription.hpp"
#include "Globals.hpp"
#include "PipelineCache.hpp"
#include "RenderStage.hpp"
#include "ShaderCache.hpp"

#include <algorithm>

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
        LogError(LC_RENDER, "Invalid shaders provided");
        return;
    }

    if (mpRenderPass == nullptr) {
        LogError(LC_RENDER, "No valid renderpass provided");
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


static Vec2u GetDescriptorIndexRangeForShader(Ref<ShaderProgram>& program)
{
    SizedArray<ShaderReflectionEntry>& refl = program->Reflection;

    uint8 min_set = 10;
    uint8 max_set = 0;

    for (const ShaderReflectionEntry& entry : refl) {
        max_set = std::max(entry.Set, max_set);
        min_set = std::min(entry.Set, min_set);
    }

    return Vec2u(static_cast<uint32>(min_set), static_cast<uint32>(max_set));
}

PipelineLayout State::BuildLayout()
{
    // Create the descriptor set layout
    {
        // Get the minimum and maximum descriptor sets used by each shader
        Vec2u vertex_ds_range = GetDescriptorIndexRangeForShader(mpVertexShader);
        Vec2u pixel_ds_range = GetDescriptorIndexRangeForShader(mpPixelShader);

        Vec2u ds_range_combined = Vec2u(std::min(vertex_ds_range.X, pixel_ds_range.X),
                                        std::max(vertex_ds_range.Y, pixel_ds_range.Y));

        const uint32 max_ds_index = ds_range_combined.Y;
        const uint32 min_ds_index = ds_range_combined.X;

        if (max_ds_index >= min_ds_index) {
            mDescriptors.Size = (max_ds_index - min_ds_index) + 1;

            AddDescriptorsForShaderProgram(mpVertexShader);
            AddDescriptorsForShaderProgram(mpPixelShader);
        }
    }

    mpPipeline->Layout = PipelineLayout(Slice(mPushConstants), Slice(mDescriptors));
    return mpPipeline->Layout;
}

void State::SetRenderPass(RenderPass* rp) { mpRenderPass = rp; }
void State::SetOutputTargets(TargetList* targets) { pOutputTargets = targets; }

void State::EndPipeline()
{
    BuildPipeline();
    Reset();
}


void State::AddDescriptorsForShaderProgram(Ref<ShaderProgram>& program)
{
    Vec2u ds_index_range = GetDescriptorIndexRangeForShader(program);

    const SizedArray<ShaderReflectionEntry>& refl = program->Reflection;

    for (int32 ds_index = ds_index_range.X; ds_index <= ds_index_range.Y; ds_index++) {
        VkDescriptorSetLayout ds_layout = gDsLayoutCache->Request(program->ShaderType, refl, ds_index);
        mDescriptors[ds_index] = (ds_layout);
    }
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
