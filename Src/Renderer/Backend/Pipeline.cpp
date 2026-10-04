
#include "Pipeline.hpp"

#include "Commands.hpp"
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
// Pipeline Layout
/////////////////////////////////////


void PipelineLayout::Create(const Slice<const PushConstants>& push_constant_defs,
							const Slice<VkDescriptorSetLayout>& descriptor_set_layouts)

{
	StackArray<RxPushConstantDef, ShaderUtil::scNumShaderTypes> rust_defs;

	for (uint32 i = 0; i < push_constant_defs.Size; i++) {
		mPushConstDefs.Insert(push_constant_defs[i]);

		rust_defs.Insert(RxPushConstantDef { .size = push_constant_defs[i].Size,
											 .stages = ShaderUtil::ToUnderlyingType(
												 push_constant_defs[i].ShaderTypes) });
	}

	static_assert(sizeof(VkDescriptorSetLayout) == sizeof(uint64));

	uint64 handle = 0;

	const VkResult status = static_cast<VkResult>(rx_gpu_pipeline_layout_create(
		gGraphics->GetDevice()->GetRustDevice(), reinterpret_cast<const uint64*>(descriptor_set_layouts.pData),
		descriptor_set_layouts.Size, rust_defs.pData, rust_defs.Size, &handle));

	if (status != VK_SUCCESS) {
		ModulePanicVulkan("Failed to create pipeline layout", status);
	}

	InternalLayout = RxFromRaw<VkPipelineLayout>(handle);
}

PipelineLayout::PipelineLayout(const PipelineLayout& other) { (*this) = other; }


PipelineLayout& PipelineLayout::operator=(const PipelineLayout& other)
{
	InheritRef(other);

	InternalLayout = other.InternalLayout;
	mPushConstDefs = other.mPushConstDefs;

	return *this;
}

void PipelineLayout::DestroyObject()
{
	if (InternalLayout == nullptr) {
		return;
	}

	rx_gpu_pipeline_layout_destroy(gGraphics->GetDevice()->GetRustDevice(), RxRaw(InternalLayout));
	InternalLayout = nullptr;
}

PipelineLayout::~PipelineLayout() { ReleaseRef(); }


/////////////////////////////////////
// Pipeline
/////////////////////////////////////

void Pipeline::Create(ePipelineName name, const Slice<Ref<ShaderProgram>>& shaders,
					  const Slice<VkAttachmentDescription>& attachments,
					  const Slice<VkPipelineColorBlendAttachmentState>& color_blend_attachments,
					  VertexDescription* vertex_info, const RenderPass& render_pass,
					  const PipelineProperties& properties)
{
	mDevice = gGraphics->GetDevice();

	Name = name;

	VertexShader = shaders[0];
	PixelShader = shaders[1];

	SizedArray<uint32> stage_flags(shaders.Size);
	SizedArray<uint64> stage_modules(shaders.Size);

	for (const Ref<ShaderProgram>& shader_program : shaders) {
		stage_flags.Insert(ShaderUtil::ToUnderlyingType(shader_program->ShaderType));
		stage_modules.Insert(RxRaw(shader_program->Get()));
	}

	SizedArray<int32> attachment_formats(attachments.Size);

	for (const VkAttachmentDescription& attachment : attachments) {
		attachment_formats.Insert(attachment.format);
	}

	DefaultCullMode = properties.CullMode;

	const RxGraphicsPipelineDesc desc = {
		.stage_flags = stage_flags.pData,
		.stage_modules = stage_modules.pData,
		.stage_count = stage_flags.Size,
		.vertex_binding = (vertex_info != nullptr) ? &vertex_info->Binding : nullptr,
		.vertex_attributes = (vertex_info != nullptr) ? vertex_info->Attributes.pData : nullptr,
		.vertex_attribute_count = (vertex_info != nullptr) ? static_cast<size_t>(vertex_info->Attributes.Size) : 0,
		.color_blend = color_blend_attachments.pData,
		.color_blend_count = color_blend_attachments.Size,
		.attachment_formats = attachment_formats.pData,
		.attachment_count = attachment_formats.Size,
		.cull_mode = properties.CullMode,
		.front_face = properties.WindingOrder,
		.polygon_mode = properties.PolygonMode,
		.depth_compare_op = properties.DepthCompareOp,
		.render_lines = properties.bRenderLines,
		.disable_depth_test = properties.bDisableDepthTest,
		.disable_depth_write = properties.bDisableDepthWrite,
		.layout = RxRaw(Layout.Get()),
		.render_pass = RxRaw(render_pass.Get()),
	};

	uint64 handle = 0;

	const VkResult status = static_cast<VkResult>(
		rx_gpu_graphics_pipeline_create(mDevice->GetRustDevice(), &desc, &handle));

	if (status != VK_SUCCESS) {
		ModulePanicVulkan("Could not create graphics pipeline", status);
	}

	InternalPipeline = RxFromRaw<VkPipeline>(handle);

	Util::SetDebugLabel(PipelineNameUtil::GetName(name), VK_OBJECT_TYPE_PIPELINE, InternalPipeline);

	LogInfo(LC_RENDER, "Creating pipeline for shader '{}' -> LayoutHandle={:p}", shaders[0]->pShader->GetName(),
			reinterpret_cast<void*>(Layout.Get()));
}

void Pipeline::CreateCompute(ePipelineName name, const Ref<ShaderProgram>& shader)
{
	mDevice = gGraphics->GetDevice();

	Name = name;
	bIsCompute = true;

	ComputeShader = shader;

	uint64 handle = 0;

	const VkResult status = static_cast<VkResult>(rx_gpu_compute_pipeline_create(
		mDevice->GetRustDevice(), RxRaw(shader->Get()), RxRaw(Layout.Get()), &handle));

	if (status != VK_SUCCESS) {
		ModulePanicVulkan("Could not create compute pipeline", status);
	}

	InternalPipeline = RxFromRaw<VkPipeline>(handle);

	Util::SetDebugLabel(PipelineNameUtil::GetName(name), VK_OBJECT_TYPE_PIPELINE, InternalPipeline);

	LogInfo(LC_RENDER, "Creating compute pipeline for shader '{}' -> LayoutHandle={:p}", shader->pShader->GetName(),
			reinterpret_cast<void*>(Layout.Get()));
}

void Pipeline::Bind(const CommandBuffer& cmd) const
{
	if (InternalPipeline == cmd.pBoundPipeline) {
		return;
	}

	RxGpuDevice* device = gGraphics->GetDevice()->GetRustDevice();

	rx_gpu_cmd_bind_pipeline(device, cmd.Cmd, GetBindPoint(), RxRaw(InternalPipeline));

	if (!bIsCompute) {
		rx_gpu_cmd_set_cull_mode(device, cmd.Cmd, DefaultCullMode);
	}

	cmd.pBoundPipeline = InternalPipeline;
}

void Pipeline::SetDoubleSided(const CommandBuffer& cmd, bool double_sided) const
{
	if (!bIsCompute) {
		rx_gpu_cmd_set_cull_mode(gGraphics->GetDevice()->GetRustDevice(), cmd.Cmd,
								 double_sided ? VK_CULL_MODE_NONE : DefaultCullMode);
	}
}

void Pipeline::Destroy()
{
	if (!mDevice || !mDevice->Device) {
		return;
	}

	// TODO: Remove this
	mDevice->WaitForIdle();

	if (InternalPipeline) {
		rx_gpu_pipeline_destroy(mDevice->GetRustDevice(), RxRaw(InternalPipeline));
		InternalPipeline = nullptr;
	}
}


} // namespace fx::renderer
