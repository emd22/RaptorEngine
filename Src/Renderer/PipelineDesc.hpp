#pragma once

#include "Backend/BlendAttachment.hpp"
#include "Backend/Pipeline.hpp"
#include "Backend/Shader.hpp"
#include "PipelineVariant.hpp"
#include "ShaderNames.hpp"
#include "Vertex.hpp"

#include <functional>
#include <string>
#include <vector>

namespace fx::renderer {

class PSOBuild;
class RenderStage;

/**
 * @brief Everything needed to build a graphics pipeline, with nothing left in a builder. Made by a pass's template (see
 * PipelineCache::RegisterPassTemplate()) when a pipeline is asked for that does not exist yet.
 */
struct PipelineDesc
{
	/// Shows up in logs and debug labels
	std::string DebugName = "Pipeline";

	eShaderName Shader = eShaderName::NumShaders;
	/// The macro names and values are not copied, so they have to be string literals
	std::vector<ShaderMacro> Macros;

	eVertexType VertexType = eVertexType::Default;
	/// The pipeline does not process any vertices (fullscreen passes)
	bool bNoVertices = false;

	/// What the pipeline draws to
	RenderStage* pStage = nullptr;

	eCullMode CullMode = eCullMode::Back;
	eFaceOrder FaceOrder = eFaceOrder::Default;
	VkCompareOp DepthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
	bool bDepthTest = true;
	bool bDepthWrite = true;
	bool bRenderLines = false;

	/// Blend state for the colour targets that are blended. BlendAttachment::TargetIndex says which target.
	std::vector<BlendAttachment> Blends;

	eShaderType PushConstantStages = eShaderType::None;
	uint32 PushConstantSize = 0;

	/// Declares the pipeline's descriptors on the builder, see PSOBuild::AddBuffer()
	std::function<void(PSOBuild&)> DeclareDescriptors;

	/// The features the pipeline draws. A pass's template sets this to the features it actually implements, which can
	/// be fewer than were asked for, see PipelineCache::GetOrCreateVariant().
	ePipelineFeatures Features = ePipelineFeatures::None;
};

} // namespace fx::renderer
