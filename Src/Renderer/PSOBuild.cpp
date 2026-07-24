#include "PSOBuild.hpp"

#include "Backend/DescriptorCache.hpp"
#include "Backend/RenderPass.hpp"
#include "Backend/VertexDescription.hpp"
#include "Globals.hpp"
#include "PipelineCache.hpp"
#include "RenderStage.hpp"
#include "ShaderCache.hpp"

#include <algorithm>

#define FX_DEBUG_SET_DESCRIPTOR_NAMES 1

namespace fx::renderer {


static constexpr uint32 scMaxNumDescriptorSets = 6;
static constexpr uint32 scMaxNumDescriptorBindings = 10;


void PSOBuild::BeginPipeline(const ePipelineName name)
{
	mPipelineName = name;
	mpPipeline = &gPipelineCache->Request(name);

	if (!mpPipeline->DescriptorIDs.IsInited()) {
		mpPipeline->DescriptorIDs.InitCapacity(8);
	}

	{
		mDescriptorEntries.InitSize(scMaxNumDescriptorSets);

		for (SizedArray<DescriptorEntry>& entry_list : mDescriptorEntries) {
			entry_list.InitCapacity(scMaxNumDescriptorBindings);
		}
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

void PSOBuild::SetLayout(ePipelineName name)
{
	mpPipeline->SetLayout(gPipelineCache->Request(name).Layout);
	ReuseDS(name);
}

bool PSOBuild::HasDescriptorsToBuild() const
{
	for (uint32 i = 0; i < mDescriptorEntries.Size; i++) {
		const SizedArray<DescriptorEntry>& desc_list = mDescriptorEntries[i];

		if (desc_list.Size > 0) {
			return true;
		}
	}

	return false;
}


void PSOBuild::BuildPipeline()
{
	if (!mpPipeline->HasLayout()) {
		CheckDescriptorsAgainstShader();
		BuildLayout();
	}
	// If there are descriptors waiting to be built but there is already a built pipeline layout, then they will never
	// be created or used. Notify incase that is a mistake.
	else if (HasDescriptorsToBuild() && !HasFlag(mFlags, ePSOBuildFlags::ReuseDescriptors)) {
		LogWarning(LC_RENDER, "PSOBuild: Descriptors will never be built for pipeline '{}'",
				   PipelineNameUtil::GetName(mPipelineName));
		Panic("PSOBuild", "Oops!");
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

	for (uint32 i = 0; i < mDescriptorEntries.Size; i++) {
		SizedArray<DescriptorEntry>& desc_list = mDescriptorEntries[i];

		LogInfo("Descriptor '{}' Size is {}", i, desc_list.Size);

		// If there are no entries added, skip creating the DS
		if (desc_list.Size == 0) {
			continue;
		}

		// Request (likely create) the descriptor set + descriptor set layout
		std::pair<Hash32, DescriptorSet*> ds_result = gDescriptorCache->Request(desc_list);

#ifdef FX_DEBUG_SET_DESCRIPTOR_NAMES
		String debug_str = String::Fmt("{}_{}_{}", PipelineNameUtil::GetName(mPipelineName), i, desc_list.Size);
		renderer::Util::SetDebugLabel(debug_str.CStr(), VK_OBJECT_TYPE_DESCRIPTOR_SET, ds_result.second->Get());
		renderer::Util::SetDebugLabel(debug_str.CStr(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
									  ds_result.second->GetLayout());
#endif

		layouts.emplace_back(ds_result.second->GetLayout());

		mpPipeline->DescriptorIDs.Emplace(i, ds_result.first);
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
			VkDescriptorSetLayout* ds_result = gDsLayoutCache->RequestExisting(ds_ref.ID);
			if (ds_result == nullptr) {
				LogWarning(LC_CORE, "PSOBuild::BuildLayout: Reused descriptor set does not exist!");
				continue;
			}

			LogInfo("Reusing DSLayout {}", ds_ref.ID);

			descriptor_layouts.emplace_back(*ds_result);
		}
	}
	else {
		// Build the descriptors from `DescriptorEntry` buffer
		descriptor_layouts = BuildDescriptorSets();
	}

	LogInfo(LC_RENDER, "Creating pipeline layout with {} descriptors", descriptor_layouts.size());

	mpPipeline->Layout = PipelineLayout(Slice(mPushConstants),
										Slice(descriptor_layouts.data(), descriptor_layouts.size()));
	return mpPipeline->Layout;
}

void PSOBuild::SetRenderPass(RenderPass* rp) { mpRenderPass = rp; }
void PSOBuild::SetOutputTargets(TargetList* targets) { pOutputTargets = targets; }

void PSOBuild::EndPipeline()
{
	LogInfo(LC_RENDER, "** Building Pipeline {} **", PipelineNameUtil::GetName(mPipelineName));
	BuildPipeline();

	LogInfo(LC_RENDER, "");
	LogInfo(LC_RENDER, "Pipeline '{}' is assigned descriptor sets: ", PipelineNameUtil::GetName(mPipelineName));

	for (Pipeline::DescriptorRef& ref : mpPipeline->DescriptorIDs) {
		LogInfo(LC_RENDER, "\tSet={}\tID={}", ref.SetIndex, ref.ID);
	}

	LogInfo(LC_RENDER, "");

	Reset();
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
}

void PSOBuild::AddImageFromTarget(uint32 bind_index, uint32 set_index, eShaderType shader_stages, Target* target,
								  Sampler* sampler)
{
	mDescriptorEntries[set_index].Insert(DescriptorEntry::AsImage(bind_index, shader_stages, &target->Image, sampler));
}

/**
 * @brief Translate shader reflection type to descriptor entry type.
 */
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


bool PSOBuild::CheckDescriptorsAgainstProgram(const Ref<ShaderProgram>& program) const
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
		return true;
	}

	return false;
}

void PSOBuild::CheckDescriptorsAgainstShader() const
{
	eShaderType shader_type = eShaderType::Vertex;

	int32 fails = 0;

	for (uint32 shader_index = 0; shader_index < ShaderNameUtil::scNumShaders - 2; shader_index++) {
		const Ref<ShaderProgram>& sp = mShaderPrograms[shader_index];
		if (!sp.IsValid()) {
			continue;
		}

		if (CheckDescriptorsAgainstProgram(sp)) {
			++fails;
		}
	}

	if (fails > 0) {
		Panic("PSOBuild", "Descriptor mismatch");
	}
}

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
}

} // namespace fx::renderer
