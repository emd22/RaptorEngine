#pragma once

#include "Backend/BlendAttachment.hpp"
#include "Backend/Pipeline.hpp"
#include "Backend/Shader.hpp"
#include "PipelineNames.hpp"
#include "ShaderNames.hpp"

#include <Core/SizedArray.hpp>

namespace fx::renderer {

class RenderPass;

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
    void SetLayout(VkPipelineLayout layout);
    void SetPushConstants(eShaderType shader_type, uint32 pc_size);
    void AddDescriptor(VkDescriptorSetLayout layout);
    VkPipelineLayout BuildLayout();

    void SetTargetBlend(uint32 target_index, const BlendAttachment& blend_attachment);
    void SetOutputTargets(TargetList* targets);

    void SetShader(eShaderName shader, const SizedArray<ShaderMacro>& macros);
    void SetVertexType(eVertexType vertex_type) { mVertexType = vertex_type; }

    void SetRenderPass(RenderPass* renderpass);

    void SetRenderLines(bool value) { mProperties.bRenderLines = value; }
    void SetFaceOrder(eFaceOrder order) { mProperties.WindingOrder = FaceOrderToVk(order); }
    void SetCullMode(eCullMode mode) { mProperties.CullMode = CullModeToVk(mode); }

private:
    void BuildPipeline();
    void Reset();

public:
    TargetList* pOutputTargets;
    BlendAttachmentList BlendAttachments;

private:
    Pipeline* mpPipeline = nullptr;
    ePipelineName mPipelineName = ePipelineName::Geometry;

    RenderPass* mpRenderPass = nullptr;

    eVertexType mVertexType = eVertexType::Default;

    Ref<ShaderProgram> mpVertexShader { nullptr };
    Ref<ShaderProgram> mpPixelShader { nullptr };

    StackArray<PushConstants, ShaderUtil::scNumShaderTypes> mPushConstants;
    SizedArray<VkDescriptorSetLayout> mDescriptors;

    PipelineProperties mProperties;
};

} // namespace fx::renderer
