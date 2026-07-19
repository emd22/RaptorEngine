#pragma once

#include "Backend/BlendAttachment.hpp"
#include "Backend/Pipeline.hpp"
#include "Backend/Shader.hpp"
#include "PipelineNames.hpp"
#include "ShaderNames.hpp"

#include <Core/SizedArray.hpp>

namespace fx {
enum class ePSOBuildFlags
{
	None = 0,
	NoVertices = (1 << 0),
	UseAutoDescriptors = (1 << 1),
};

FxEnumFlags(ePSOBuildFlags);
} // namespace fx

namespace fx::renderer {

class RenderPass;
class RenderStage;

class PSOBuild
{
public:
	/**
	 * @brief Selects a pipeline to create.
	 */
	void BeginPipeline(ePipelineName pipeline);
	void EndPipeline();

	/**
	 * @brief Sets the pipeline layout from a preexisting layout.
	 */
	void SetLayout(const PipelineLayout& layout);
	/**
	 * @brief Sets the pipeline layout to the layout used by pipeline `other_pl`
	 */
	void SetLayout(ePipelineName other_pl);

	void SetPushConstants(eShaderType shader_type, uint32 pc_size);
	PipelineLayout BuildLayout();

	void SetTargetBlend(uint32 target_index, const BlendAttachment& blend_attachment);
	void SetOutputTargets(TargetList* targets);
	void UseRenderStage(RenderStage& stage);

	/////////////////////////////////////
	// Shader/Descriptor functions
	/////////////////////////////////////
	void SetShader(eShaderName shader, const SizedArray<ShaderMacro>& macros);

	void AddBuffer(uint32 bind_index, uint32 set_index, RawGpuBuffer* buffer, uint64 offset, uint64 range);
	void AddImage(uint32 bind_index, uint32 set_index, Image* image, Sampler* sampler);
	void AddImageFromTarget(uint32 bind_index, uint32 set_index, Target* target, Sampler* sampler);


	FX_FORCE_INLINE void SetVertexType(eVertexType vertex_type) { mVertexType = vertex_type; }

	FX_FORCE_INLINE void SetFlags(ePSOBuildFlags flags) { mFlags = flags; }
	FX_FORCE_INLINE ePSOBuildFlags GetFlags() const { return mFlags; }

	FX_FORCE_INLINE void SetDepthTest(bool value) { mProperties.bDisableDepthTest = !value; }
	FX_FORCE_INLINE void SetDepthWrite(bool value) { mProperties.bDisableDepthWrite = !value; }

	void SetRenderPass(RenderPass* rp);

	void SetRenderLines(bool value) { mProperties.bRenderLines = value; }
	void SetFaceOrder(eFaceOrder order) { mProperties.WindingOrder = FaceOrderToVk(order); }
	void SetCullMode(eCullMode mode) { mProperties.CullMode = CullModeToVk(mode); }

	FX_FORCE_INLINE void SetViewportSize(const Vec2u& size) { mProperties.ViewportSize = size; }
	FX_FORCE_INLINE void SetDepthCompareOp(VkCompareOp op) { mProperties.DepthCompareOp = op; }

	FX_FORCE_INLINE Ref<ShaderProgram> GetShaderProgram(eShaderType shader_type)
	{
		return mShaderPrograms[static_cast<uint32>(shader_type) - 1];
	}

	FX_FORCE_INLINE void SetShaderProgram(eShaderType shader_type, const Ref<ShaderProgram>& program)
	{
		mShaderPrograms[static_cast<uint32>(shader_type) - 1] = program;
	}

	FX_FORCE_INLINE void EnableAutoDescriptors() { SetFlag(mFlags, ePSOBuildFlags::UseAutoDescriptors); }

private:
	void BuildPipeline();
	void BuildDescriptorLayouts();
	void Reset();


	DescriptorSet* StartDescriptorUpdate(uint32 set_index);

	void AddDescriptorsForShaderProgram(Pipeline& pl, Ref<ShaderProgram>& program);

public:
	TargetList* pOutputTargets;
	BlendAttachmentList BlendAttachments;

private:
	Pipeline* mpPipeline = nullptr;
	ePipelineName mPipelineName = ePipelineName::Geometry;

	RenderPass* mpRenderPass = nullptr;

	eVertexType mVertexType = eVertexType::Default;

	StackArray<Ref<ShaderProgram>, (ShaderUtil::scNumShaderTypes - 1)> mShaderPrograms;

	StackArray<PushConstants, ShaderUtil::scNumShaderTypes> mPushConstants;
	SizedArray<VkDescriptorSetLayout> mDescriptorLayouts;

	PipelineProperties mProperties;

	bool bBuiltDescriptorLayouts = false;

	ePSOBuildFlags mFlags = ePSOBuildFlags::None;
};

} // namespace fx::renderer
