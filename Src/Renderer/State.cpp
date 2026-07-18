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

void RenderState::BeginPipeline(ePipelineName name)
{
	mPipelineName = name;
	mpPipeline = &gPipelineCache->Request(name);

	if (!mDescriptorLayouts.IsInited()) {
		mDescriptorLayouts.InitCapacity(10);
	}
}

void RenderState::SetShader(eShaderName shader_name, const SizedArray<ShaderMacro>& macros)
{
	Ref<Shader> shader = gShaderCache->Request(shader_name);

	SetShaderProgram(eShaderType::Vertex, shader->GetProgram(eShaderType::Vertex, macros));
	SetShaderProgram(eShaderType::Pixel, shader->GetProgram(eShaderType::Pixel, macros));
	SetShaderProgram(eShaderType::Compute, shader->GetProgram(eShaderType::Compute, macros));
}

void RenderState::UseRenderStage(RenderStage& stage)
{
	SetOutputTargets(&stage.GetTargets());
	SetRenderPass(&stage.GetRenderPass());
}

void RenderState::SetLayout(const PipelineLayout& layout) { mpPipeline->SetLayout(layout); }
void RenderState::SetLayout(ePipelineName name) { mpPipeline->SetLayout(gPipelineCache->Request(name).Layout); }

void RenderState::BuildPipeline()
{
	if (!mpPipeline->HasLayout()) {
		BuildLayout();
	}

	Ref<ShaderProgram> vertex_shader = GetShaderProgram(eShaderType::Vertex);
	Ref<ShaderProgram> pixel_shader = GetShaderProgram(eShaderType::Pixel);


	if (!vertex_shader.IsValid() || !pixel_shader.IsValid()) {
		LogError(LC_RENDER, "Invalid shaders provided");
		return;
	}

	if (mpRenderPass == nullptr) {
		LogError(LC_RENDER, "No valid renderpass provided");
		return;
	}

	SizedArray<Ref<ShaderProgram>> shader_list = { vertex_shader, pixel_shader };

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

void RenderState::SetTargetBlend(uint32 target_index, const BlendAttachment& blend_attachment)
{
	BlendAttachments.AddAttachment(target_index, blend_attachment);
}


void RenderState::SetPushConstants(eShaderType type, uint32 pc_size)
{
	PushConstants* pc = mPushConstants.Insert();

	pc->ShaderTypes = type;
	pc->Size = pc_size;
}


static Vec2u GetDescriptorIndexRangeForShader(const Ref<ShaderProgram>& program)
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

PipelineLayout RenderState::BuildLayout()
{
	// Create the descriptor set layout
	{
		Pipeline& pl = gPipelineCache->Request(mPipelineName);
		pl.DescriptorIDs.InitCapacity(8);

		// Get the minimum and maximum descriptor sets used by each shader
		Vec2u vertex_ds_range = GetDescriptorIndexRangeForShader(GetShaderProgram(eShaderType::Vertex));
		Vec2u pixel_ds_range = GetDescriptorIndexRangeForShader(GetShaderProgram(eShaderType::Pixel));

		Vec2u ds_range_combined = Vec2u(std::min(vertex_ds_range.X, pixel_ds_range.X),
										std::max(vertex_ds_range.Y, pixel_ds_range.Y));

		const uint32 max_ds_index = ds_range_combined.Y;
		const uint32 min_ds_index = ds_range_combined.X;

		if (max_ds_index >= min_ds_index) {
			mDescriptorLayouts.Size = (max_ds_index - min_ds_index) + 1;

			for (Ref<ShaderProgram>& program : mShaderPrograms) {
				AddDescriptorsForShaderProgram(pl, program);
			}
		}
	}

	mpPipeline->Layout = PipelineLayout(Slice(mPushConstants), Slice(mDescriptorLayouts));
	return mpPipeline->Layout;
}

void RenderState::SetRenderPass(RenderPass* rp) { mpRenderPass = rp; }
void RenderState::SetOutputTargets(TargetList* targets) { pOutputTargets = targets; }

void RenderState::EndPipeline()
{
	BuildPipeline();
	Reset();
}

DescriptorSet* RenderState::GetDescriptorSet(eShaderType shader_type, uint32 set_index)
{
	Pipeline& pl = gPipelineCache->Request(mPipelineName);
	if (!pl.DescriptorIDs.IsInited()) {
		return nullptr;
	}

	return nullptr;
}


void RenderState::AddBuffer(uint32 bind_index, uint32 set_index, RawGpuBuffer* buffer, uint64 offset, uint64 range)
{
	Pipeline& pl = gPipelineCache->Request(mPipelineName);
	DescriptorSet* set = gDescriptorCache->Request(pl.DescriptorIDs[set_index]);

	Assert(set != nullptr);
	set->AddBuffer(bind_index, buffer, offset, range);
}

void RenderState::AddImage(uint32 bind_index, uint32 set_index, Image* image, Sampler* sampler)
{
	Pipeline& pl = gPipelineCache->Request(mPipelineName);
	DescriptorSet* set = gDescriptorCache->Request(pl.DescriptorIDs[set_index]);

	Assert(set != nullptr);
	set->AddImage(bind_index, image, sampler);
}

void RenderState::AddImageFromTarget(uint32 bind_index, uint32 set_index, Target* target, Sampler* sampler)
{
	Pipeline& pl = gPipelineCache->Request(mPipelineName);
	DescriptorSet* set = gDescriptorCache->Request(pl.DescriptorIDs[set_index]);

	Assert(set != nullptr);
	set->AddImageFromTarget(bind_index, target, sampler);
}


void RenderState::AddDescriptorsForShaderProgram(Pipeline& pl, Ref<ShaderProgram>& program)
{
	if (!program.IsValid()) {
		return;
	}

	Vec2u ds_index_range = GetDescriptorIndexRangeForShader(program);

	const SizedArray<ShaderReflectionEntry>& refl = program->Reflection;

	int32 max_index = 0;

	for (int32 ds_index = ds_index_range.X; ds_index <= ds_index_range.Y; ds_index++) {
		if (ds_index > max_index) {
			max_index = ds_index;
		}

		Hash32 descriptor_id = gDsLayoutCache->GetID(program->ShaderType,
													 gDsLayoutCache->GetEntriesForSet(refl, ds_index));


		VkDescriptorSetLayout ds_layout = gDsLayoutCache->Request(program->ShaderType, refl, ds_index);
		mDescriptorLayouts[ds_index] = ds_layout;
		pl.DescriptorIDs[ds_index] = descriptor_id;

		gDescriptorCache->Request(program->ShaderType, refl, ds_index);
	}

	// Set the size to be the max descriptor set index
	pl.DescriptorIDs.Size = std::max(static_cast<int32>(pl.DescriptorIDs.Size), max_index);
}

void RenderState::Reset()
{
	mpPipeline = nullptr;
	mpRenderPass = nullptr;

	mPushConstants.Clear();
	mDescriptorLayouts.Clear();

	BlendAttachments.Clear();

	memset(mPushConstants.pData, 0, mPushConstants.GetSizeInBytes());

	mProperties = PipelineProperties {};
	mFlags = eStateFlags::None;
}

} // namespace fx::renderer
