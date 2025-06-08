
#include "RvkPipeline.hpp"
#include "Core/Defines.hpp"
#include "Renderer/Backend/Vulkan/RvkCommands.hpp"
#include "vulkan/vulkan_core.h"

#include <Core/Log.hpp>

#include <Renderer/Renderer.hpp>

#include <Core/FxPanic.hpp>

namespace vulkan {

FX_SET_MODULE_NAME("Pipeline")

VertexInfo RvkGraphicsPipeline::MakeVertexInfo() {
    using VertexType = RvkVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>;

    VkVertexInputBindingDescription binding_desc = {
        .binding = 0,
        .stride = sizeof(VertexType),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    FxStaticArray<VkVertexInputAttributeDescription> attribs = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0 },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(VertexType, Normal) },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(VertexType, UV) },
    };

    Log::Debug("Amount of attributes: %d", attribs.Size);

    return { binding_desc, std::move(attribs) };
}

void RvkGraphicsPipeline::Create(ShaderList shader_list) {
    mDevice = RendererVulkan->GetDevice();

    VkSpecializationInfo specialization_info = {
        .mapEntryCount = 0,
        .pMapEntries = nullptr,
        .dataSize = 0,
        .pData = nullptr,
    };

    FxStaticArray<ShaderInfo> shader_stages = shader_list.GetShaderStages();
    FxStaticArray<VkPipelineShaderStageCreateInfo> shader_create_info(shader_stages.Size);

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

    FxStaticArray<VkDynamicState> dynamic_states = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamic_state_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = (uint32)dynamic_states.Size,
        .pDynamicStates = dynamic_states,
    };

    VertexInfo vertex_info = MakeVertexInfo();

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vertex_info.binding,
        .vertexAttributeDescriptionCount = (uint32)vertex_info.attributes.Size,
        .pVertexAttributeDescriptions = vertex_info.attributes.Data,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    const Vec2u extent = RendererVulkan->Swapchain.Extent;

    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float32)extent.Width(),
        .height = (float32)extent.Height(),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
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


    VkPipelineRasterizationStateCreateInfo rasterizer_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1.0f,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
    };


    VkPipelineMultisampleStateCreateInfo multisampling_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .sampleShadingEnable = VK_FALSE,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };


    VkPipelineColorBlendAttachmentState color_blend_attachment {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                        | VK_COLOR_COMPONENT_G_BIT
                        | VK_COLOR_COMPONENT_B_BIT
                        | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable = VK_FALSE,
    };


    VkPipelineColorBlendStateCreateInfo color_blend_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
    };


    CreateLayout();
    RenderPass.Create(*mDevice, RendererVulkan->Swapchain);

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
        .pDepthStencilState = &depth_stencil_info,

        .pDynamicState = &dynamic_state_info,

        .layout = Layout,

        .renderPass = RenderPass.RenderPass,
        .subpass = 0,
    };

    const VkResult status = vkCreateGraphicsPipelines(mDevice->Device, nullptr, 1, &pipeline_info, nullptr, &Pipeline);

    if (status != VK_SUCCESS) {
        FxModulePanic("Could not create graphics pipeline", status);
    }
}

void RvkGraphicsPipeline::Bind(RvkCommandBuffer &command_buffer) {
    vkCmdBindPipeline(command_buffer.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
}

void RvkGraphicsPipeline::Destroy()
{
    mDevice->WaitForIdle();

    if (Pipeline) {
        vkDestroyPipeline(mDevice->Device, Pipeline, nullptr);
    }
    if (Layout) {
        vkDestroyPipelineLayout(mDevice->Device, Layout, nullptr);
    }
    if (DescriptorSetLayout) {
        vkDestroyDescriptorSetLayout(mDevice->Device, DescriptorSetLayout, nullptr);
    }

    RenderPass.Destroy(*mDevice);
}

void RvkGraphicsPipeline::CreateLayout() {
    VkPushConstantRange buffer_range{};
    buffer_range.offset = 0;
    buffer_range.size = sizeof(DrawPushConstants);
    buffer_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;


    // TODO: move descriptor set layout creation out of here
    VkDescriptorSetLayoutBinding ubo_layout_binding {
        .binding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .pImmutableSamplers = nullptr,
    };

    // VkDescriptorSetLayoutBinding sampler_layout_binding {
    //     .binding = 1,
    //     .descriptorCount = 1,
    //     .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    //     .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    //     .pImmutableSamplers = nullptr,
    // };

    VkDescriptorSetLayoutBinding bindings[] = {
        ubo_layout_binding, //sampler_layout_binding,
    };

    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = sizeof(bindings) / sizeof(bindings[0]),
        .pBindings = bindings,
    };

    VkResult status;

    status = vkCreateDescriptorSetLayout(mDevice->Device, &descriptor_set_layout_info, nullptr, &DescriptorSetLayout);
    if (status != VK_SUCCESS) {
        FxModulePanic("Failed to create pipeline descriptor set layout", status);
    }


    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &DescriptorSetLayout,

        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &buffer_range,

    };

    status = vkCreatePipelineLayout(mDevice->Device, &create_info, nullptr, &Layout);

    if (status != VK_SUCCESS) {
        FxModulePanic("Failed to create pipeline layout", status);
    }
}

}; // namespace vulkan
