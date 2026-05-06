
#include "Pipeline.hpp"

#include "Commands.hpp"
#include "Shader.hpp"
#include "VertexDescription.hpp"

#include <Core/Defines.hpp>
#include <Core/Panic.hpp>
#include <Core/StackArray.hpp>
#include <Renderer/DeferredRenderer.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

FX_SET_MODULE_NAME("Pipeline")

namespace fx::renderer {

static VkPipeline spBoundPipeline = nullptr;

void Pipeline::Create(const std::string& name, const Slice<Ref<ShaderProgram>>& shaders,
                      const Slice<VkAttachmentDescription>& attachments,
                      const Slice<VkPipelineColorBlendAttachmentState>& color_blend_attachments,
                      VertexDescription* vertex_info, const RenderPass& render_pass,
                      const PipelineProperties& properties)
{
    mDevice = gRenderer->GetDevice();

    Name = name;

    bool has_depth_attachment = false;

    // XXX: TEMP
    VertexShader = shaders[0];
    FragmentShader = shaders[1];

    // Depth attachment is usually the last attachment, check last first
    for (int32 i = attachments.Size - 1; i >= 0; i--) {
        if (Util::IsFormatDepth(attachments[i].format)) {
            has_depth_attachment = true;
        }
    }

    if (!has_depth_attachment) {
        LogInfo("Pipeline '{}' does not have a depth attachment", name);
    }

    VkSpecializationInfo specialization_info = {
        .mapEntryCount = 0,
        .pMapEntries = nullptr,
        .dataSize = 0,
        .pData = nullptr,
    };

    // Shaders
    SizedArray<VkPipelineShaderStageCreateInfo> shader_create_info(shaders.Size);

    for (const Ref<ShaderProgram>& shader_program : shaders) {
        const VkPipelineShaderStageCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = static_cast<VkShaderStageFlagBits>(ShaderUtil::ToUnderlyingType(shader_program->ShaderType)),
            .module = shader_program->Get(),
            .pName = "main",
            .pSpecializationInfo = &specialization_info,
        };

        LogDebug("Added shader (Vertex?: {})", (create_info.stage == VK_SHADER_STAGE_VERTEX_BIT));

        shader_create_info.Insert(create_info);
    }

    // Dynamic states (scissor & viewport updates dynamically)
    SizedArray<VkDynamicState> dynamic_states = {};

    const VkPipelineDynamicStateCreateInfo dynamic_state_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32>(dynamic_states.Size),
        .pDynamicStates = dynamic_states,
    };

    Vec2u viewport_size = properties.ViewportSize;

    // If there is no viewport size passed in, assume the swapchain size.
    if (properties.ViewportSize.X == 0 || properties.ViewportSize.Y == 0) {
        viewport_size = gRenderer->Swapchain.Extent;
    }


    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float32>(viewport_size.X),
        .height = static_cast<float32>(viewport_size.Y),
        .minDepth = 1.0f,
        .maxDepth = 0.0f,
    };

    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = { .width = viewport_size.X, .height = viewport_size.Y },
    };

    const VkPipelineViewportStateCreateInfo viewport_state_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    // Vertex info + input info
    VkPipelineVertexInputStateCreateInfo vertex_input_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };

    if (vertex_info != nullptr) {
        vertex_input_info.vertexBindingDescriptionCount = 1;
        vertex_input_info.pVertexBindingDescriptions = &vertex_info->Binding;
        vertex_input_info.vertexAttributeDescriptionCount = static_cast<uint32>(vertex_info->Attributes.Size);
        vertex_input_info.pVertexAttributeDescriptions = vertex_info->Attributes.pData;
    }

    const VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = (properties.bRenderLines) ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    const VkPipelineRasterizationStateCreateInfo rasterizer_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = properties.PolygonMode,
        .cullMode = properties.CullMode,
        .frontFace = properties.WindingOrder,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f,
    };


    const VkPipelineMultisampleStateCreateInfo multisampling_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };


    const VkPipelineColorBlendStateCreateInfo color_blend_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = color_blend_attachments.Size,
        .pAttachments = color_blend_attachments.pData,
    };

    VkBool32 depth_test_enabled = VK_TRUE;
    VkBool32 depth_write_enabled = VK_TRUE;

    if (properties.bDisableDepthTest) {
        depth_test_enabled = VK_FALSE;
    }
    if (properties.bDisableDepthWrite) {
        depth_write_enabled = VK_FALSE;
    }

    // Unused if has_depth_attachment is false
    const VkPipelineDepthStencilStateCreateInfo depth_stencil_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = depth_test_enabled,
        .depthWriteEnable = depth_write_enabled,
        .depthCompareOp = properties.DepthCompareOp,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };

    const VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,

        .stageCount = (uint32)shader_create_info.Size,
        .pStages = shader_create_info,

        .pVertexInputState = &vertex_input_info,
        .pInputAssemblyState = &input_assembly_info,
        .pViewportState = &viewport_state_info,
        .pRasterizationState = &rasterizer_info,
        .pMultisampleState = &multisampling_info,
        .pDepthStencilState = has_depth_attachment ? &depth_stencil_info : nullptr,
        .pColorBlendState = &color_blend_info,

        .pDynamicState = &dynamic_state_info,

        .layout = Layout,

        .renderPass = render_pass.RenderPass,
        .subpass = 0,
    };

    const VkResult status = vkCreateGraphicsPipelines(mDevice->Device, nullptr, 1, &pipeline_info, nullptr,
                                                      &InternalPipeline);

    if (status != VK_SUCCESS) {
        ModulePanicVulkan("Could not create graphics pipeline", status);
    }

    Util::SetDebugLabel(name.c_str(), VK_OBJECT_TYPE_PIPELINE, InternalPipeline);

    LogInfo("Creating pipeline for shader '{}' -> LayoutHandle={:p}", shaders[0]->pShader->GetName(),
            reinterpret_cast<void*>(Layout));
}

void Pipeline::Bind(const CommandBuffer& cmd)
{
    if (this->InternalPipeline == spBoundPipeline) {
        return;
    }

    vkCmdBindPipeline(cmd.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, InternalPipeline);

    spBoundPipeline = this->InternalPipeline;
}

void Pipeline::Destroy()
{
    if (!mDevice || !mDevice->Device) {
        return;
    }

    mDevice->WaitForIdle();

    if (InternalPipeline) {
        vkDestroyPipeline(mDevice->Device, InternalPipeline, nullptr);
        InternalPipeline = nullptr;
    }
    if (Layout && !mbDoNotDestroyLayout) {
        vkDestroyPipelineLayout(mDevice->Device, Layout, nullptr);
    }

    Layout = nullptr;
    // RenderPass.Destroy();
}


VkPipelineLayout Pipeline::CreateLayout(const Slice<const PushConstants>& push_constant_defs,
                                        const Slice<VkDescriptorSetLayout>& descriptor_set_layouts)
{
    StackArray<VkPushConstantRange, 3> push_const_ranges;

    // VkPushConstantRange pc_ranges[3];
    // uint32 pc_ranges_count = 0;
    //
    uint32 current_pc_offset = 0;

    for (int i = 0; i < push_constant_defs.Size; i++) {
        const PushConstants& pc_def = push_constant_defs[i];

        if (pc_def.ShaderTypes == static_cast<eShaderType>(0)) {
            continue;
        }

        LogDebug("Adding push constant (Off={}, Sz={})", current_pc_offset, pc_def.Size);

        VkPushConstantRange range {
            .stageFlags = ShaderUtil::ToUnderlyingType(pc_def.ShaderTypes),
            .offset = current_pc_offset,
            .size = pc_def.Size,
        };
        push_const_ranges.Insert(range);

        current_pc_offset += pc_def.Size;
    }

    VkPipelineLayoutCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = descriptor_set_layouts.Size,
        .pSetLayouts = descriptor_set_layouts.pData,

        .pushConstantRangeCount = push_const_ranges.Size,
        .pPushConstantRanges = push_const_ranges.pData,
    };

    VkPipelineLayout layout;
    VkResult status = vkCreatePipelineLayout(gRenderer->GetDevice()->Device, &create_info, nullptr, &layout);

    if (status != VK_SUCCESS) {
        ModulePanicVulkan("Failed to create pipeline layout", status);
    }

    LogDebug("Creating pipeline layout {:p}", reinterpret_cast<void*>(layout));

    return layout;
}

} // namespace fx::renderer
