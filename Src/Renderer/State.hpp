#pragma once

#include "Backend/BlendAttachment.hpp"
#include "Backend/Pipeline.hpp"
#include "Backend/Shader.hpp"
#include "PipelineNames.hpp"
#include "ShaderNames.hpp"

#include <Core/SizedArray.hpp>


namespace fx::renderer {

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

    void SetTargetBlend(uint32 target_index, const BlendAttachment& blend_attachment);
    void SetOutputTargets(const TargetList& targets);

    void SetPushConstants();

    void SetShader(eShaderName shader, const SizedArray<ShaderMacro>& macros);
    void SetVertexType(eVertexType vertex_type) { mVertexType = vertex_type; }

private:
    void BuildPipeline();
    void Reset();

public:
    TargetList OutputTargets;
    BlendAttachmentList BlendAttachments;

private:
    Pipeline* mpPipeline = nullptr;
    ePipelineName mPipelineName = ePipelineName::Geometry;

    eVertexType mVertexType = eVertexType::Default;

    Ref<ShaderProgram> mpVertexShader { nullptr };
    Ref<ShaderProgram> mpPixelShader { nullptr };

    StackArray<PushConstants, ShaderUtil::scNumShaderTypes> mPushConstants;
};


// class RenderPass;

// class State
// {
// public:
//     State();

//     void BufferOffset(eShaderType shader_type, uint32 buffer_offset);
//     void Pipeline(Pipeline* pipeline);
//     void RenderPass(RenderPass* rp);

//     void Apply(const CommandBuffer& cmd);

//     void Reset();

//     ~State();

// private:
//     Pipeline* mpPipeline;
//     RenderPass* mpRenderPass = nullptr;

//     StackArray<ShaderBindOptions, ShaderUtil::scNumShaderTypes> mBindOptions;
// };

} // namespace fx::renderer
