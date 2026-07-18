#pragma once

#include "Backend/BlendAttachment.hpp"
#include "Backend/Pipeline.hpp"
#include "Backend/Shader.hpp"
#include "PipelineNames.hpp"
#include "ShaderNames.hpp"

#include <Core/SizedArray.hpp>

namespace fx {
enum class eStateFlags
{
	None = 0,
	NoVertices = (1 << 0),
};

FxEnumFlags(eStateFlags);
} // namespace fx

namespace fx::renderer {

class RenderPass;
class RenderStage;

class State
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

	void AddBuffer(eShaderType shader_type, uint32 bind_index, RawGpuBuffer* buffer, uint64 offset, uint64 range);
	void AddImage(eShaderType shader_type, uint32 bind_index, Image* image, Sampler* sampler);
	void AddImageFromTarget(eShaderType shader_type, uint32 bind_index, Target* target, Sampler* sampler);


	FX_FORCE_INLINE void SetVertexType(eVertexType vertex_type) { mVertexType = vertex_type; }

	FX_FORCE_INLINE void SetFlags(eStateFlags flags) { mFlags = flags; }
	FX_FORCE_INLINE eStateFlags GetFlags() const { return mFlags; }

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

private:
	void BuildPipeline();
	void Reset();

	void AddDescriptorsForShaderProgram(Ref<ShaderProgram>& program);

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

	eStateFlags mFlags = eStateFlags::None;
};

} // namespace fx::renderer
