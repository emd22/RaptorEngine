#pragma once

#include "Backend/BlendAttachment.hpp"
#include "Backend/Pipeline.hpp"
#include "Backend/Shader.hpp"
#include "PipelineKey.hpp"
#include "PipelineNames.hpp"
#include "PipelineVariant.hpp"
#include "ShaderNames.hpp"
#include "Target.hpp"

#include <functional>
#include <span>

namespace fx {
enum class ePSOBuildFlags
{
	None = 0,

	/// The pipeline does not process any vertices.
	NoVertices = (1 << 0),

};

FxEnumFlags(ePSOBuildFlags);
} // namespace fx

namespace fx::renderer {

class RenderStage;

/**
 * @brief Everything needed to build a pipeline. The description is held and the pipeline built by Rust, and this
 * fills one in.
 *
 * The fixed pipelines are built by selecting one with BeginPipeline() and finishing it with EndPipeline() on the
 * global builder. The others are described by the pass templates (see PipelineCache::RegisterPassTemplate()), which are
 * given a builder for the description to fill in.
 */
class PSOBuild
{
public:
	using DeclareFunction = std::function<void(PSOBuild&)>;

	PSOBuild() = default;
	explicit PSOBuild(RxPipelineDesc* desc) : mpDesc(desc) {}

	/**
	 * @brief Selects a pipeline to create.
	 */
	void BeginPipeline(const ePipelineName pipeline);
	void EndPipeline();

	void SetDebugName(const char* name);

	void SetPushConstants(eShaderType shader_type, uint32 pc_size);

	void SetTargetBlend(uint32 target_index, const BlendAttachment& blend_attachment);
	void UseRenderStage(RenderStage& stage);

	/**
	 * @brief Declares the pipeline's descriptors when it is built, which can be later and on another thread than the
	 * description is filled in on.
	 */
	void SetDeclareDescriptors(DeclareFunction declare);

	/// The features that the pipeline draws. A pass's template sets this to the features it actually implements, which
	/// can be fewer than were asked for, see PipelineCache::GetOrCreateVariant().
	void SetFeatures(ePipelineFeatures features);

	/////////////////////////////////////
	// Shader/Descriptor functions
	/////////////////////////////////////

	/// The macro names and values are not copied until the pipeline is built, so they have to be string literals
	void SetShader(eShaderName shader, std::span<const ShaderMacro> macros);

	void AddBuffer(uint32 bind_index, uint32 set_index, eShaderType shader_stages, RawGpuBuffer* buffer, uint64 offset,
				   uint64 range);
	void AddImage(uint32 bind_index, uint32 set_index, eShaderType shader_stages, Image* image, Sampler* sampler);
	void AddImageFromTarget(uint32 bind_index, uint32 set_index, eShaderType shader_stages, const TargetRef& target,
							Sampler* sampler);

	/////////////////////////////////////
	// Modifiers
	/////////////////////////////////////

	void SetVertexType(eVertexType vertex_type);

	void SetFlags(ePSOBuildFlags flags);

	void SetDepthTest(bool value);
	void SetDepthWrite(bool value);

	void SetRenderLines(bool value);
	void SetFaceOrder(eFaceOrder order);
	void SetCullMode(eCullMode mode);

	void SetDepthCompareOp(VkCompareOp op);

private:
	RxPipelineDesc* mpDesc = nullptr;

	/// The fixed pipeline that BeginPipeline() selected
	ePipelineName mPipelineName = ePipelineName::NumPipelines;
};

} // namespace fx::renderer
