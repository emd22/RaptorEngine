#include "PSOBuild.hpp"

#include "Backend/DescriptorCache.hpp"
#include "GraphicsBackend.hpp"
#include "Globals.hpp"
#include "PipelineCache.hpp"
#include "RenderStage.hpp"

namespace fx::renderer {

void PSOBuild::BeginPipeline(const ePipelineName name)
{
	AssertMsg(mpDesc == nullptr, "A pipeline is already being built");

	mPipelineName = name;
	mpDesc = rx_pipeline_desc_new();

	const char* debug_name = PipelineNameUtil::GetName(name);

	SetDebugName(debug_name);
	SetCullMode(eCullMode::None);

	rx_pipeline_cache_set_static_name(gPipelineCache->GetRust(), static_cast<uint32>(name), debug_name);
}

void PSOBuild::EndPipeline()
{
	AssertMsg(mpDesc != nullptr, "There is no pipeline being built");

	RxPipelineDesc* desc = mpDesc;

	mpDesc = nullptr;

	gPipelineCache->BuildStatic(PipelineCache::GetHandle(mPipelineName), desc);

	mPipelineName = ePipelineName::NumPipelines;
}

void PSOBuild::SetDebugName(const char* name) { rx_pipeline_desc_set_debug_name(mpDesc, name); }

void PSOBuild::SetPushConstants(eShaderType type, uint32 pc_size)
{
	rx_pipeline_desc_add_push_constants(mpDesc, pc_size, static_cast<uint32>(type));
}

void PSOBuild::SetTargetBlend(uint32 target_index, const BlendAttachment& blend)
{
	const RxBlendAttachment rust_blend = {
		.enabled = blend.Enabled,
		.write_mask = static_cast<uint32>(blend.Mask),
		.color_op = blend.BlendOp.Ops.Color,
		.alpha_op = blend.BlendOp.Ops.Alpha,
		.src_color = blend.ColorBlend.Ops.Src,
		.dst_color = blend.ColorBlend.Ops.Dst,
		.src_alpha = blend.AlphaBlend.Ops.Src,
		.dst_alpha = blend.AlphaBlend.Ops.Dst,
		.target_index = blend.TargetIndex,
	};

	rx_pipeline_desc_add_blend(mpDesc, target_index, &rust_blend);
}

void PSOBuild::UseRenderStage(RenderStage& stage) { rx_pipeline_desc_set_stage(mpDesc, stage.GetRust()); }

void PSOBuild::SetDeclareDescriptors(DeclareFunction declare)
{
	rx_pipeline_desc_set_declare(
		mpDesc,
		[](void* user, RxPipelineDesc* desc)
		{
			PSOBuild pso(desc);
			(*static_cast<DeclareFunction*>(user))(pso);
		},
		new DeclareFunction(std::move(declare)), [](void* user) { delete static_cast<DeclareFunction*>(user); });
}

void PSOBuild::SetFeatures(ePipelineFeatures features)
{
	rx_pipeline_desc_set_features(mpDesc, static_cast<uint32>(features));
}

void PSOBuild::SetShader(eShaderName shader, std::span<const ShaderMacro> macros)
{
	rx_pipeline_desc_set_shader(mpDesc, static_cast<uint32>(shader), ShaderNameUtil::GetName(shader),
								reinterpret_cast<const RxShaderMacroRef*>(macros.data()), macros.size());
}

void PSOBuild::AddBuffer(uint32 bind_index, uint32 set_index, eShaderType shader_stages, RawGpuBuffer* buffer,
						 uint64 offset, uint64 range)
{
	const RxDescriptorEntry entry =
		ToRustEntry(DescriptorEntry::AsBuffer(bind_index, shader_stages, buffer, offset, range));

	rx_pipeline_desc_add_entry(mpDesc, set_index, &entry);
}

void PSOBuild::AddImage(uint32 bind_index, uint32 set_index, eShaderType shader_stages, Image* image, Sampler* sampler)
{
	const RxDescriptorEntry entry = ToRustEntry(DescriptorEntry::AsImage(bind_index, shader_stages, image, sampler));

	rx_pipeline_desc_add_entry(mpDesc, set_index, &entry);
}

void PSOBuild::AddImageFromTarget(uint32 bind_index, uint32 set_index, eShaderType shader_stages,
								  const TargetRef& target, Sampler* sampler)
{
	AssertMsg(target.IsValid(), "Target must be valid");

	const RxDescriptorEntry entry =
		ToRustEntry(DescriptorEntry::AsImage(bind_index, shader_stages, target.GetRecord(), sampler));

	rx_pipeline_desc_add_entry(mpDesc, set_index, &entry);
}

void PSOBuild::SetVertexType(eVertexType vertex_type)
{
	rx_pipeline_desc_set_vertex_type(mpDesc, static_cast<uint32>(vertex_type));
}

void PSOBuild::SetFlags(ePSOBuildFlags flags)
{
	rx_pipeline_desc_set_no_vertices(mpDesc, (flags & ePSOBuildFlags::NoVertices) != 0);
}

void PSOBuild::SetDepthTest(bool value) { rx_pipeline_desc_set_depth_test(mpDesc, value); }
void PSOBuild::SetDepthWrite(bool value) { rx_pipeline_desc_set_depth_write(mpDesc, value); }
void PSOBuild::SetRenderLines(bool value) { rx_pipeline_desc_set_render_lines(mpDesc, value); }

void PSOBuild::SetFaceOrder(eFaceOrder order) { rx_pipeline_desc_set_front_face(mpDesc, FaceOrderToVk(order)); }
void PSOBuild::SetCullMode(eCullMode mode) { rx_pipeline_desc_set_cull_mode(mpDesc, CullModeToVk(mode)); }

void PSOBuild::SetDepthCompareOp(VkCompareOp op) { rx_pipeline_desc_set_depth_compare_op(mpDesc, op); }

} // namespace fx::renderer
