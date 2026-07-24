#include "PSOBuild.hpp"

#include "Backend/DescriptorCache.hpp"
#include "Backend/RenderPass.hpp"
#include "Backend/VertexDescription.hpp"
#include "Globals.hpp"
#include "PipelineCache.hpp"
#include "RenderStage.hpp"
#include "ShaderCache.hpp"

#include <algorithm>

namespace fx::renderer {


static constexpr uint32 scMaxNumDescriptorSets = 6;
static constexpr uint32 scMaxNumDescriptorBindings = 10;


void PSOBuild::BeginPipeline(ePipelineName name)
{
	mPipelineName = name;
	mpPipeline = &gPipelineCache->Request(name);

	// if (!mDescriptorLayouts.IsInited()) {
	// 	mDescriptorLayouts.InitCapacity(10);
	// }

	if (!mpPipeline->DescriptorIDs.IsInited()) {
		mpPipeline->DescriptorIDs.InitCapacity(8);
	}

	{
		mDescriptorEntries.InitSize(scMaxNumDescriptorSets);

		for (SizedArray<DescriptorEntry>& entry_list : mDescriptorEntries) {
			entry_list.InitCapacity(scMaxNumDescriptorBindings);
		}

		mDescriptorEntries.Size = 0;
	}
}

void PSOBuild::SetShader(eShaderName shader_name, const SizedArray<ShaderMacro>& macros)
{
	Ref<Shader> shader = gShaderCache->Request(shader_name);

	SetShaderProgram(eShaderType::Vertex, shader->GetProgram(eShaderType::Vertex, macros));
	SetShaderProgram(eShaderType::Pixel, shader->GetProgram(eShaderType::Pixel, macros));
	SetShaderProgram(eShaderType::Compute, shader->GetProgram(eShaderType::Compute, macros));
}

void PSOBuild::UseRenderStage(RenderStage& stage)
{
	SetOutputTargets(&stage.GetTargets());
	SetRenderPass(&stage.GetRenderPass());
}

void PSOBuild::SetLayout(const PipelineLayout& layout) { mpPipeline->SetLayout(layout); }
void PSOBuild::SetLayout(ePipelineName name) { mpPipeline->SetLayout(gPipelineCache->Request(name).Layout); }

void PSOBuild::BuildPipeline()
{
	// BuildDescriptorLayouts();
	CheckDescriptorsAgainstShader();

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
	if ((mFlags & ePSOBuildFlags::NoVertices) == 0) {
		vertex_ptr = &vertex_desc;
	}

	// Since the blend attachments apply _only_ to colour targets, we want to get the amount of non-depth targets.
	SizedArray<Target> color_only_targets = pOutputTargets->GetTargetByType(eImageAspectFlag::Color);
	SizedArray<VkPipelineColorBlendAttachmentState> blend_attachments = BlendAttachments.GetVkAttachments(
		color_only_targets.Size);

	mpPipeline->Create(mPipelineName, shader_list, pOutputTargets->GetDescriptions(), blend_attachments, vertex_ptr,
					   *mpRenderPass, mProperties);

	// if (!HasFlag(mFlags, ePSOBuildFlags::UseAutoDescriptors)) {
	mpPipeline->DescriptorIDs.Free();
	mpPipeline->bBindAttachedDescriptors = false;
	return;
	// }


	mpPipeline->bBindAttachedDescriptors = true;

	// Build any descriptors that were created
	for (uint32 i = 0; i < mpPipeline->DescriptorIDs.Size; i++) {
		Pipeline::DescriptorRef& desc_ref = mpPipeline->DescriptorIDs[i];

		if (desc_ref.ID == HashNull32) {
			LogWarning("Skipping DS(index {}) as it is null.", i);
			continue;
		}

		DescriptorSet* ds = gDescriptorCache->Request(desc_ref.ID);
		if (ds == nullptr) {
			LogError(LC_RENDER, "RenderState: Descriptor set could not be built (not found)");
			LogError(LC_RENDER, "RenderState: Descriptor set ID is {} ({} logged descriptors)", desc_ref.ID,
					 mpPipeline->DescriptorIDs.Size);
			continue;
		}

		ds->Build();
	}
}

void PSOBuild::SetTargetBlend(uint32 target_index, const BlendAttachment& blend_attachment)
{
	BlendAttachments.AddAttachment(target_index, blend_attachment);
}


void PSOBuild::SetPushConstants(eShaderType type, uint32 pc_size)
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

std::vector<VkDescriptorSetLayout> PSOBuild::BuildDescriptorSets()
{
	std::vector<VkDescriptorSetLayout> layouts;
	layouts.reserve(8);

	for (uint32 i = 0; i < mDescriptorEntries.Capacity; i++) {
		SizedArray<DescriptorEntry>& desc_list = mDescriptorEntries[i];

		// If there are no entries added, skip creating the DS
		if (desc_list.Size == 0) {
			continue;
		}

		// Request (likely create) the descriptor set + descriptor set layout
		DescriptorSet* ds = gDescriptorCache->Request(desc_list);
		layouts.emplace_back(ds->GetLayout());
	}

	return layouts;
}

PipelineLayout PSOBuild::BuildLayout()
{
	std::vector<VkDescriptorSetLayout> descriptor_layouts;

	if (HasFlag(mFlags, ePSOBuildFlags::ReuseDescriptors)) {
		// Retrieve the descriptor layouts from the actively created descriptors. The list on the pipeline should
		// already be filled out on call of `ReuseDS`.
		descriptor_layouts.reserve(mpPipeline->DescriptorIDs.Size);

		for (Pipeline::DescriptorRef& ds_ref : mpPipeline->DescriptorIDs) {
			// Finally, we can get the layout from the previously created DS.
			std::pair<Hash32, VkDescriptorSetLayout> ds_result = gDsLayoutCache->Request(ds_ref.ID);
			if (ds_result.second == nullptr) {
				LogWarning(LC_CORE, "PSOBuild::BuildLayout: Reused descriptor set does not exist!");
				continue;
			}

			LogInfo("Reusing DSLayout {}", ds_ref.ID);

			descriptor_layouts.emplace_back(ds_result.second);
		}
	}
	else {
		// Build the descriptors from `DescriptorEntry` buffer
		descriptor_layouts = BuildDescriptorSets();
	}

	mpPipeline->Layout = PipelineLayout(Slice(mPushConstants),
										Slice(descriptor_layouts.data(), descriptor_layouts.size()));
	return mpPipeline->Layout;
}

void PSOBuild::SetRenderPass(RenderPass* rp) { mpRenderPass = rp; }
void PSOBuild::SetOutputTargets(TargetList* targets) { pOutputTargets = targets; }

void PSOBuild::EndPipeline()
{
	BuildPipeline();
	Reset();
}

DescriptorSet* PSOBuild::StartDescriptorUpdate(uint32 set_index)
{
	// AssertMsg(mpPipeline != nullptr, "Cannot call descriptor update function outside of active RenderState");

	// BuildDescriptorLayouts();

	// DescriptorSet* set = nullptr;
	// for (uint32 i = 0; i < mpPipeline->DescriptorIDs.Size; i++) {
	// 	Pipeline::DescriptorRef& ref = mpPipeline->DescriptorIDs[i];
	// 	if (ref.SetIndex == set_index) {
	// 		set = gDescriptorCache->Request(ref.ID);
	// 		AssertMsg(set != nullptr, "StartDescriptorUpdate: Cannot retrieve descriptor set");
	// 		return set;
	// 	}
	// }

	return nullptr;
}


void PSOBuild::ReuseDS(ePipelineName other_pso)
{
	// Copy the descriptor ref's from the other pso
	SetFlag(mFlags, ePSOBuildFlags::ReuseDescriptors);
	mpPipeline->DescriptorIDs.CloneFrom(gPipelineCache->Request(other_pso).DescriptorIDs);
}

void PSOBuild::AddExistingDS(uint32 set_index, Hash32 layout_id)
{
	mpPipeline->DescriptorIDs.Emplace(set_index, layout_id);
}

void PSOBuild::AddBuffer(uint32 bind_index, uint32 set_index, eShaderType shader_stages, RawGpuBuffer* buffer,
						 uint64 offset, uint64 range)
{
	mDescriptorEntries[set_index].Insert(DescriptorEntry::AsBuffer(bind_index, shader_stages, buffer, offset, range));
}

void PSOBuild::AddImage(uint32 bind_index, uint32 set_index, eShaderType shader_stages, Image* image, Sampler* sampler)
{
	mDescriptorEntries[set_index].Insert(DescriptorEntry::AsImage(bind_index, shader_stages, image, sampler));

	// DescriptorSet* set = StartDescriptorUpdate(set_index);
	// set->AddImage(bind_index, image, sampler);
}

void PSOBuild::AddImageFromTarget(uint32 bind_index, uint32 set_index, eShaderType shader_stages, Target* target,
								  Sampler* sampler)
{
	mDescriptorEntries[set_index].Insert(DescriptorEntry::AsImage(bind_index, shader_stages, &target->Image, sampler));

	// DescriptorSet* set = StartDescriptorUpdate(set_index);
	// set->AddImageFromTarget(bind_index, target, sampler);
}

static eDescriptorEntryType SRTToDET(eShaderReflectionType srt)
{
	switch (srt) {
	case eShaderReflectionType::StructuredBuffer:
		[[fallthrough]];
	case eShaderReflectionType::CBuffer:
		return eDescriptorEntryType::Buffer;
	case eShaderReflectionType::Texture:
		return eDescriptorEntryType::Image;
	default:;
	}

	return eDescriptorEntryType::None;
}


void PSOBuild::CheckDescriptorsAgainstProgram(const Ref<ShaderProgram>& program) const
{
	int32 num_errors = 0;

	for (const ShaderReflectionEntry& refl_entry : program->Reflection) {
		eDescriptorEntryType det = SRTToDET(refl_entry.Type);

		bool is_valid = false;

		for (const DescriptorEntry& ds_entry : mDescriptorEntries[refl_entry.Set]) {
			if (ds_entry.Binding == refl_entry.Binding && det == ds_entry.Type) {
				is_valid = true;
				break;
			}
		}

		if (!is_valid) {
			LogError(LC_RENDER, "PSOBuild: Missing descriptor (Binding={}, Set={}) of type '{}'", refl_entry.Binding,
					 refl_entry.Set, DescriptorEntryUtil::GetTypeName(det));
			++num_errors;
			break;
		}
	}

	if (num_errors > 0) {
		LogError(LC_RENDER, "PSOBuild: Failed when builing '{}' ({})", program->pShader->GetName(),
				 ShaderUtil::TypeToName(program->ShaderType));
	}
}

void PSOBuild::CheckDescriptorsAgainstShader() const
{
	eShaderType shader_type = eShaderType::Vertex;

	for (uint32 shader_index = 0; shader_index < ShaderNameUtil::scNumShaders - 1; shader_index++) {
		CheckDescriptorsAgainstProgram(mShaderPrograms[shader_index]);
	}
}


// void PSOBuild::AddDescriptorsForShaderProgram(Pipeline& pl, Ref<ShaderProgram>& program)
// {
// 	if (!program.IsValid()) {
// 		return;
// 	}

// 	Vec2u ds_index_range = GetDescriptorIndexRangeForShader(program);

// 	const SizedArray<ShaderReflectionEntry>& refl = program->Reflection;

// 	for (int32 ds_index = ds_index_range.X; ds_index <= ds_index_range.Y; ds_index++) {
// 		LogInfo("Building Descriptor Set {}:", ds_index);


// 		Hash32 descriptor_id = gDsLayoutCache->GetID(program->ShaderType,
// 													 gDsLayoutCache->GetEntriesForSet(refl, ds_index));

// 		Assert(descriptor_id != HashNull32);

// 		VkDescriptorSetLayout ds_layout = gDsLayoutCache->Request(program->ShaderType, refl, ds_index);
// 		mDescriptorLayouts[ds_index] = ds_layout;

// 		pl.DescriptorIDs.Insert(
// 			Pipeline::DescriptorRef { .SetIndex = static_cast<uint32>(ds_index), .ID = descriptor_id });
// 		LogInfo("Adding Descriptor {} at set index {}", descriptor_id, ds_index);

// 		gDescriptorCache->Request(program->ShaderType, refl, ds_index);
// 	}


// 	// Set the size to be the max descriptor set index
// 	// pl.DescriptorIDs.Size = std::max(static_cast<int32>(pl.DescriptorIDs.Size), max_index);
// }

void PSOBuild::Reset()
{
	mpPipeline = nullptr;
	mpRenderPass = nullptr;
	mVertexType = eVertexType::Default;
	// bBuiltDescriptorLayouts = false;

	mPushConstants.Clear();
	// mDescriptorLayouts.Clear();

	BlendAttachments.Clear();

	memset(mPushConstants.pData, 0, mPushConstants.GetSizeInBytes());

	mProperties = PipelineProperties {};
	mFlags = ePSOBuildFlags::None;

	// Reset the saved descriptor entries
	for (SizedArray<DescriptorEntry>& entry_list : mDescriptorEntries) {
		entry_list.Clear();
	}

	// Do not call clear here as we don't want each entry list to be freed.
	// We'll just do the cheap of clearing the size.
	mDescriptorEntries.Size = 0;
}

} // namespace fx::renderer
