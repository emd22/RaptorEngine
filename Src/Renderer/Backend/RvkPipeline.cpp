
#include "RvkPipeline.hpp"
#include "Core/Defines.hpp"
#include "RvkCommands.hpp"

#include <Core/Log.hpp>
#include <Core/FxPanic.hpp>

#include <Renderer/Renderer.hpp>
#include <Renderer/FxDeferred.hpp>

FX_SET_MODULE_NAME("Pipeline")

FxVertexInfo FxMakeVertexInfo()
{
    using VertexType = RvkVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>;

    VkVertexInputBindingDescription binding_desc = {
        .binding = 0,
        .stride = sizeof(VertexType),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    FxSizedArray<VkVertexInputAttributeDescription> attribs = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0 },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(VertexType, Normal) },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(VertexType, UV) },
    };

    Log::Debug("Amount of attributes: %d", attribs.Size);

    return { binding_desc, std::move(attribs) };
}

FxVertexInfo FxMakeLightVertexInfo()
{
    using VertexType = RvkVertex<FxVertexPosition>;

    VkVertexInputBindingDescription binding_desc = {
        .binding = 0,
        .stride = sizeof(VertexType),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    FxSizedArray<VkVertexInputAttributeDescription> attribs = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0 },
    };

    return { binding_desc, std::move(attribs) };
}

void RvkGraphicsPipeline::Create(
    const std::string& name,
    ShaderList shader_list,
    VkPipelineLayout layout,
    const FxSlice<VkAttachmentDescription>& attachments,
    const FxSlice<VkPipelineColorBlendAttachmentState>& color_blend_attachments,
    FxVertexInfo* vertex_info,
    VkCullModeFlags cull_mode,
    bool winding_is_ccw
)
{
    mDevice = Renderer->GetDevice();
    Layout = layout;

    bool has_depth_attachment = false;

    // Depth attachment is usually the last attachment, check last first
    for (int32 i = attachments.Size - 1; i >= 0; --i) {
        if (RvkUtil::IsFormatDepth(attachments[i].format)) {
            has_depth_attachment = true;
        }
    }

    VkSpecializationInfo specialization_info = {
        .mapEntryCount = 0,
        .pMapEntries = nullptr,
        .dataSize = 0,
        .pData = nullptr,
    };

    // Shaders
    FxSizedArray<ShaderInfo> shader_stages = shader_list.GetShaderStages();
    FxSizedArray<VkPipelineShaderStageCreateInfo> shader_create_info(shader_stages.Size);

    for (ShaderInfo stage : shader_stages) {
        const VkPipelineShaderStageCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = stage.GetStageBit(),
            .module = stage.ShaderModule,
            .pName = "main",
            .pSpecializationInfo = &specialization_info,
        };

        Log::Debug("Added shader (Vertex?: %s)", Log::YesNo(create_info.stage == VK_SHADER_STAGE_VERTEX_BIT));

        shader_create_info.Insert(create_info);
    }

    // Dynamic states (scissor & viewport updates dynamically)
    FxSizedArray<VkDynamicState> dynamic_states = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamic_state_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = (uint32)dynamic_states.Size,
        .pDynamicStates = dynamic_states,
    };

    const FxVec2u extent = Renderer->Swapchain.Extent;

    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float32)extent.Width(),
        .height = (float32)extent.Height(),
        .minDepth = 1.0f,
        .maxDepth = 0.0f,
    };

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = {.width = (uint32)extent.Width(), .height = (uint32)extent.Height() },
    };

    VkPipelineViewportStateCreateInfo viewport_state_info = {
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
        vertex_input_info.pVertexBindingDescriptions = &vertex_info->binding;
        vertex_input_info.vertexAttributeDescriptionCount = static_cast<uint32>(vertex_info->attributes.Size);
        vertex_input_info.pVertexAttributeDescriptions = vertex_info->attributes.Data;
    }

    VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1.0f,
        .cullMode = cull_mode,
        .frontFace = winding_is_ccw ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
    };


    VkPipelineMultisampleStateCreateInfo multisampling_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .sampleShadingEnable = VK_FALSE,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };




    VkPipelineColorBlendStateCreateInfo color_blend_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = color_blend_attachments.Size,
        .pAttachments = color_blend_attachments,
    };

    RenderPass.Create2(attachments);

    // Unused if has_depth_attachment is false
    VkPipelineDepthStencilStateCreateInfo depth_stencil_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_GREATER,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };

    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,

        .stageCount = (uint32)shader_create_info.Size,
        .pStages = shader_create_info,

        .pVertexInputState = &vertex_input_info,
        .pInputAssemblyState = &input_assembly_info,
        .pViewportState = &viewport_state_info,
        .pRasterizationState = &rasterizer_info,
        .pMultisampleState = &multisampling_info,
        .pColorBlendState = &color_blend_info,
        .pDepthStencilState = has_depth_attachment ? &depth_stencil_info : nullptr,

        .pDynamicState = &dynamic_state_info,

        .layout = Layout,

        .renderPass = RenderPass.RenderPass,
        .subpass = 0,
    };

    const VkResult status = vkCreateGraphicsPipelines(mDevice->Device, nullptr, 1, &pipeline_info, nullptr, &Pipeline);

    if (status != VK_SUCCESS) {
        FxModulePanic("Could not create graphics pipeline", status);
    }

    RvkUtil::SetDebugLabel(name.c_str(), VK_OBJECT_TYPE_PIPELINE, Pipeline);
}

// void RvkGraphicsPipeline::CreateComp(ShaderList shader_list, VkPipelineLayout layout, const FxSlice<VkPipelineColorBlendAttachmentState>& color_blend_attachments, bool is_comp) {
//     mDevice = Renderer->GetDevice();
//     Layout = layout;

//     VkSpecializationInfo specialization_info = {
//         .mapEntryCount = 0,
//         .pMapEntries = nullptr,
//         .dataSize = 0,
//         .pData = nullptr,
//     };

//     FxSizedArray<ShaderInfo> shader_stages = shader_list.GetShaderStages();
//     FxSizedArray<VkPipelineShaderStageCreateInfo> shader_create_info(shader_stages.Size);

//     for (ShaderInfo stage : shader_stages) {
//         const VkPipelineShaderStageCreateInfo create_info = {
//             .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
//             .stage = stage.GetStageBit(),
//             .module = stage.ShaderModule,
//             .pName = "main",
//             .pSpecializationInfo = &specialization_info,
//         };

//         Log::Debug("Added shader (Vertex?: %s)", Log::YesNo(create_info.stage == VK_SHADER_STAGE_VERTEX_BIT));

//         shader_create_info.Insert(create_info);
//     }

//     FxSizedArray<VkDynamicState> dynamic_states = {
//         VK_DYNAMIC_STATE_VIEWPORT,
//         VK_DYNAMIC_STATE_SCISSOR
//     };

//     VkPipelineDynamicStateCreateInfo dynamic_state_info = {
//         .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
//         .dynamicStateCount = (uint32)dynamic_states.Size,
//         .pDynamicStates = dynamic_states,
//     };

//     VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
//         .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
//         .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
//         .primitiveRestartEnable = VK_FALSE,
//     };

//     const Vec2u extent = Renderer->Swapchain.Extent;

//     VkViewport viewport = {
//         .x = 0.0f,
//         .y = 0.0f,
//         .width = (float32)extent.Width(),
//         .height = (float32)extent.Height(),
//         .minDepth = 0.0f,
//         .maxDepth = 1.0f,
//     };


//     VkRect2D scissor = {
//         .offset = { 0, 0 },
//         .extent = {.width = (uint32)extent.Width(), .height = (uint32)extent.Height() },
//     };


//     VkPipelineViewportStateCreateInfo viewport_state_info = {
//         .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
//         .viewportCount = 1,
//         .pViewports = &viewport,
//         .scissorCount = 1,
//         .pScissors = &scissor,
//     };


//     VkPipelineRasterizationStateCreateInfo rasterizer_info = {
//         .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
//         .depthClampEnable = VK_FALSE,
//         .rasterizerDiscardEnable = VK_FALSE,
//         .polygonMode = VK_POLYGON_MODE_FILL,
//         .lineWidth = 1.0f,
//         .cullMode = VK_CULL_MODE_NONE,
//         .frontFace = VK_FRONT_FACE_CLOCKWISE,
//         .depthBiasEnable = VK_FALSE,
//     };


//     VkPipelineMultisampleStateCreateInfo multisampling_info {
//         .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
//         .sampleShadingEnable = VK_FALSE,
//         .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
//     };

//     VkPipelineDepthStencilStateCreateInfo depth_stencil_info {
//         .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
//         .depthTestEnable = VK_TRUE,
//         .depthWriteEnable = VK_TRUE,
//         .depthCompareOp = VK_COMPARE_OP_LESS,
//         .depthBoundsTestEnable = VK_FALSE,
//         .stencilTestEnable = VK_FALSE,
//     };


//     // VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
//     //     VkPipelineColorBlendAttachmentState {
//     //         .colorWriteMask = VK_COLOR_COMPONENT_R_BIT
//     //                         | VK_COLOR_COMPONENT_G_BIT
//     //                         | VK_COLOR_COMPONENT_B_BIT
//     //                         | VK_COLOR_COMPONENT_A_BIT,
//     //         .blendEnable = VK_FALSE,
//     //     },
//     //     VkPipelineColorBlendAttachmentState {
//     //         .colorWriteMask = VK_COLOR_COMPONENT_R_BIT
//     //                         | VK_COLOR_COMPONENT_G_BIT
//     //                         | VK_COLOR_COMPONENT_B_BIT
//     //                         | VK_COLOR_COMPONENT_A_BIT,
//     //         .blendEnable = VK_FALSE,
//     //     },
//     // };


//     VkPipelineColorBlendStateCreateInfo color_blend_info {
//         .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
//         .logicOpEnable = VK_FALSE,
//         .attachmentCount = color_blend_attachments.Size,
//         .pAttachments = color_blend_attachments,
//     };

//     FxAssert(is_comp == true);
//     RenderPass.CreateComp(*mDevice, Renderer->Swapchain);


//     VkPipelineVertexInputStateCreateInfo empty_vertex_input_info {};
// 	empty_vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

//     VkGraphicsPipelineCreateInfo pipeline_info = {
//         .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,

//         .stageCount = (uint32)shader_create_info.Size,
//         .pStages = shader_create_info,

//         .pVertexInputState = &empty_vertex_input_info,
//         .pInputAssemblyState = &input_assembly_info,
//         .pViewportState = &viewport_state_info,
//         .pRasterizationState = &rasterizer_info,
//         .pMultisampleState = &multisampling_info,
//         .pColorBlendState = &color_blend_info,
//         .pDepthStencilState = &depth_stencil_info,

//         .pDynamicState = &dynamic_state_info,

//         .layout = Layout,

//         .renderPass = RenderPass.RenderPass,
//         .subpass = 0,
//     };

//     const VkResult status = vkCreateGraphicsPipelines(mDevice->Device, nullptr, 1, &pipeline_info, nullptr, &Pipeline);

//     if (status != VK_SUCCESS) {
//         FxModulePanic("Could not create graphics pipeline", status);
//     }

//     printf("Create pipeline %p\n", Pipeline);
// }

void RvkGraphicsPipeline::Bind(RvkCommandBuffer &command_buffer) {
    vkCmdBindPipeline(command_buffer.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
}

void RvkGraphicsPipeline::Destroy()
{
    if (!mDevice || !mDevice->Device) {
        return;
    }

    mDevice->WaitForIdle();

    if (Pipeline) {
        vkDestroyPipeline(mDevice->Device, Pipeline, nullptr);
        Pipeline = nullptr;
    }
    if (Layout) {
        vkDestroyPipelineLayout(mDevice->Device, Layout, nullptr);
        Layout = nullptr;
    }

    if (CompDescriptorSetLayout) {
        vkDestroyDescriptorSetLayout(mDevice->Device, CompDescriptorSetLayout, nullptr);
        CompDescriptorSetLayout = nullptr;
    }

    RenderPass.Destroy();
}


VkPipelineLayout RvkGraphicsPipeline::CreateLayout(
    uint32 vert_push_consts_size,
    uint32 frag_push_consts_size,
    const FxSlice<VkDescriptorSetLayout>& descriptor_set_layouts
)
{
    if (mDevice == nullptr) {
        mDevice = Renderer->GetDevice();
    }

    VkPushConstantRange pc_ranges[2];
    uint32 pc_ranges_count = 0;

    if (vert_push_consts_size) {
        VkPushConstantRange& range = pc_ranges[0];
        range.size = vert_push_consts_size;
        range.offset = 0;
        range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        ++pc_ranges_count;
    }

    if (frag_push_consts_size) {
        VkPushConstantRange& range = pc_ranges[pc_ranges_count];
        range.size = frag_push_consts_size;
        range.offset = vert_push_consts_size ? vert_push_consts_size : 0;
        range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        ++pc_ranges_count;
    }

    VkPipelineLayoutCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = descriptor_set_layouts.Size,
        .pSetLayouts = descriptor_set_layouts.Ptr,

        .pushConstantRangeCount = pc_ranges_count,
        .pPushConstantRanges = pc_ranges
    };

    VkPipelineLayout layout;
    VkResult status = vkCreatePipelineLayout(mDevice->Device, &create_info, nullptr, &layout);

    if (status != VK_SUCCESS) {
        FxModulePanic("Failed to create pipeline layout", status);
    }

    return layout;
}

// VkPipelineLayout RvkGraphicsPipeline::CreateCompLayout() {
//     if (mDevice == nullptr) {
//         mDevice = Renderer->GetDevice();
//     }

//     VkDescriptorSetLayoutBinding positions_layout_binding {
//         .binding = 1,
//         .descriptorCount = 1,
//         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
//         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
//         .pImmutableSamplers = nullptr,
//     };

//     VkDescriptorSetLayoutBinding albedo_layout_binding {
//         .binding = 2,
//         .descriptorCount = 1,
//         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
//         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
//         .pImmutableSamplers = nullptr,
//     };

//     VkDescriptorSetLayoutBinding bindings[] = {
//         positions_layout_binding,
//         albedo_layout_binding
//     };

//     VkDescriptorSetLayoutCreateInfo comp_layout_info {
//         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
//         .bindingCount = FxSizeofArray(bindings),
//         .pBindings = bindings,
//     };

//     VkResult status;
//     status = vkCreateDescriptorSetLayout(mDevice->Device, &comp_layout_info, nullptr, &CompDescriptorSetLayout);
//     if (status != VK_SUCCESS) {
//         FxModulePanic("Failed to create pipeline descriptor set layout", status);
//     }

//     VkPipelineLayoutCreateInfo create_info = {
//         .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
//         .setLayoutCount = 1,
//         .pSetLayouts = &CompDescriptorSetLayout,

//         .pushConstantRangeCount = 0,
//         .pPushConstantRanges = nullptr,

//     };

//     VkPipelineLayout layout;

//     status = vkCreatePipelineLayout(mDevice->Device, &create_info, nullptr, &layout);

//     if (status != VK_SUCCESS) {
//         FxModulePanic("Failed to create pipeline layout", status);
//     }

//     return layout;
// }
