
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
	Release();

	StackArray<RxPushConstantDef, ShaderUtil::scNumShaderTypes> rust_defs;

	for (uint32 i = 0; i < push_constant_defs.Size; i++) {
		rust_defs.Insert(RxPushConstantDef { .size = push_constant_defs[i].Size,
											 .stages = ShaderUtil::ToUnderlyingType(
												 push_constant_defs[i].ShaderTypes) });
	}

	static_assert(sizeof(VkDescriptorSetLayout) == sizeof(uint64));

	int32 status = 0;

	mpRecord = rx_pipeline_layout_new(gGraphics->GetDevice()->GetRustDevice(),
									  reinterpret_cast<const uint64*>(descriptor_set_layouts.pData),
									  descriptor_set_layouts.Size, rust_defs.pData, rust_defs.Size, &status);

	if (mpRecord == nullptr) {
		ModulePanicVulkan("Failed to create pipeline layout", static_cast<VkResult>(status));
	}
}

PipelineLayout::PipelineLayout(const PipelineLayout& other) : mpRecord(other.mpRecord)
{
	if (mpRecord != nullptr) {
		rx_pipeline_layout_retain(mpRecord);
	}
}

PipelineLayout::PipelineLayout(PipelineLayout&& other) noexcept : mpRecord(other.mpRecord) { other.mpRecord = nullptr; }

PipelineLayout& PipelineLayout::operator=(const PipelineLayout& other)
{
	if (mpRecord == other.mpRecord) {
		return *this;
	}

	Release();

	mpRecord = other.mpRecord;

	if (mpRecord != nullptr) {
		rx_pipeline_layout_retain(mpRecord);
	}

	return *this;
}

PipelineLayout& PipelineLayout::operator=(PipelineLayout&& other) noexcept
{
	if (this == &other) {
		return *this;
	}

	Release();

	mpRecord = other.mpRecord;
	other.mpRecord = nullptr;

	return *this;
}

void PipelineLayout::Release()
{
	if (mpRecord == nullptr) {
		return;
	}

	GraphicsBackend* graphics = gGraphics;

	rx_pipeline_layout_release(mpRecord, (graphics != nullptr) ? graphics->GetDevice()->GetRustDevice() : nullptr);
	mpRecord = nullptr;
}

PipelineLayout::~PipelineLayout() { Release(); }


/////////////////////////////////////
// Pipeline
/////////////////////////////////////

void Pipeline::Create(ePipelineName name, const Slice<ShaderProgram>& shaders,
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

	for (const ShaderProgram& shader_program : shaders) {
		stage_flags.Insert(ShaderUtil::ToUnderlyingType(shader_program.GetType()));
		stage_modules.Insert(RxRaw(shader_program.Get()));
	}

	SizedArray<int32> attachment_formats(attachments.Size);

	for (const VkAttachmentDescription& attachment : attachments) {
		attachment_formats.Insert(attachment.format);
	}

	Destroy();

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

	int32 status = 0;

	mpRecord = rx_pipeline_create_graphics(mDevice->GetRustDevice(), &desc, &status);

	if (mpRecord == nullptr) {
		ModulePanicVulkan("Could not create graphics pipeline", static_cast<VkResult>(status));
	}

	Util::SetDebugLabel(PipelineNameUtil::GetName(name), VK_OBJECT_TYPE_PIPELINE, Get());

	LogInfo(LC_RENDER, "Creating pipeline for shader '{}' -> LayoutHandle={:p}", shaders[0].GetShader()->GetName(),
			reinterpret_cast<void*>(Layout.Get()));
}

void Pipeline::CreateCompute(ePipelineName name, const ShaderProgram& shader)
{
	mDevice = gGraphics->GetDevice();

	Name = name;

	ComputeShader = shader;

	Destroy();

	int32 status = 0;

	mpRecord = rx_pipeline_create_compute(mDevice->GetRustDevice(), RxRaw(shader.Get()), RxRaw(Layout.Get()),
										  &status);

	if (mpRecord == nullptr) {
		ModulePanicVulkan("Could not create compute pipeline", static_cast<VkResult>(status));
	}

	Util::SetDebugLabel(PipelineNameUtil::GetName(name), VK_OBJECT_TYPE_PIPELINE, Get());

	LogInfo(LC_RENDER, "Creating compute pipeline for shader '{}' -> LayoutHandle={:p}", shader.GetShader()->GetName(),
			reinterpret_cast<void*>(Layout.Get()));
}

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

void Pipeline::Destroy()
{
	if (!mDevice || !mDevice->Device) {
		return;
	}

	// TODO: Remove this
	mDevice->WaitForIdle();

	if (mpRecord != nullptr) {
		rx_pipeline_destroy(mpRecord, mDevice->GetRustDevice());
		mpRecord = nullptr;
	}
}


} // namespace fx::renderer
