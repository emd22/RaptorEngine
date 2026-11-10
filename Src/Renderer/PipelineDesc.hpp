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

struct PipelineDesc
{
	std::string DebugName = "Pipeline";

	eShaderName Shader = eShaderName::NumShaders;
	std::vector<ShaderMacro> Macros;

	eVertexType VertexType = eVertexType::Default;

	/// The pipeline does not process any vertices (e.g. fullscreen passes)
	bool bNoVertices = false;

	/// What the pipeline draws to
	RenderStage* pStage = nullptr;

	eCullMode CullMode = eCullMode::Back;
	eFaceOrder FaceOrder = eFaceOrder::Default;
	VkCompareOp DepthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

	float32 DepthBiasConstant = 0.0f;
	float32 DepthBiasSlope = 0.0f;

	bool bDepthTest = true;
	bool bDepthWrite = true;
	bool bRenderLines = false;

	/// Blend state for the colour targets that are blended
	std::vector<BlendAttachment> Blends;

	eShaderType PushConstantStages = eShaderType::None;
	uint32 PushConstantSize = 0;

	/// Declares the pipeline's descriptors on the builder
	std::function<void(PSOBuild&)> DeclareDescriptors;

	/// The features the pipeline draws
	ePipelineFeatures Features = ePipelineFeatures::None;
};

} // namespace fx::renderer
