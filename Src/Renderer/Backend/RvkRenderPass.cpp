#include <vulkan/vulkan.h>
#include <stdexcept>

#include <Renderer/Renderer.hpp>

#include "RvkDevice.hpp"
#include "RvkRenderPass.hpp"

#include "RvkSwapchain.hpp"
#include "vulkan/vulkan_core.h"

#include <Core/FxPanic.hpp>

FX_SET_MODULE_NAME("RvkRenderPass")

void RvkRenderPass::Create(RvkGpuDevice &device, RvkSwapchain &swapchain)
{
    VkAttachmentReference color_refs[] = {
        // Albedo attachment
        VkAttachmentReference{
            .attachment = 0,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
        // Positions attachment
        VkAttachmentReference{
            .attachment = 1,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
    };


    VkAttachmentReference depth_attachment_ref {
        .attachment = 2,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };


    VkSubpassDescription subpass {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = sizeof(color_refs) / sizeof(color_refs[0]),
        .pColorAttachments = color_refs,
        .pDepthStencilAttachment = &depth_attachment_ref,
        .pResolveAttachments = nullptr,
    };

    // VkSubpassDependency subpass_dependency = {
    //     .srcSubpass = VK_SUBPASS_EXTERNAL,
    //     .dstSubpass = 0,
    //     .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    //         | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
    //     .srcAccessMask = 0,
    //     .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    //         | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
    //     .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
    //             | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    // };


    VkSubpassDependency subpass_dependencies[] = {
        VkSubpassDependency {
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,

            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                          | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,

            .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,

            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                           | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,

            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,

            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        },
        VkSubpassDependency {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,

            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,

            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                          | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,

            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,

            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                           | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,

            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        }
    };


    VkAttachmentDescription depth_attachment {
        .format = VK_FORMAT_D16_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    FxSizedArray attachments = {
        // Positions attachment
        VkAttachmentDescription {
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,

            // .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },
        // Albedo attachment
        VkAttachmentDescription {
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        },

        depth_attachment
    };

    VkRenderPassCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = static_cast<uint32>(attachments.Size),
        .pAttachments = attachments.Data,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = sizeof(subpass_dependencies) / sizeof(subpass_dependencies[0]),
        .pDependencies = subpass_dependencies,
    };

    const VkResult status = vkCreateRenderPass(device.Device, &create_info, nullptr, &RenderPass);
    if (status != VK_SUCCESS) {
        FxModulePanic("Failed to create render pass", status);
    }
}


void RvkRenderPass::CreateComp(RvkGpuDevice &device, RvkSwapchain &swapchain)
{
    VkAttachmentReference color_refs[] = {
        // Albedo attachment
        VkAttachmentReference{
            .attachment = 0,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
    };


    // VkAttachmentReference depth_attachment_ref {
    //     .attachment = 1,
    //     .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    // };


    VkSubpassDescription subpass {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = sizeof(color_refs) / sizeof(color_refs[0]),
        .pColorAttachments = color_refs,
        .pDepthStencilAttachment = nullptr,
        .pResolveAttachments = nullptr,
    };

    // VkSubpassDependency subpass_dependency = {
    //     .srcSubpass = VK_SUBPASS_EXTERNAL,
    //     .dstSubpass = 0,
    //     .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    //         | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
    //     .srcAccessMask = 0,
    //     .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    //         | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
    //     .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
    //             | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    // };


    VkSubpassDependency subpass_dependencies[] = {
        VkSubpassDependency {
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,

            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                          | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,

            .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,

            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                           | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,

            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,

            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        },
        VkSubpassDependency {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,

            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,

            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                          | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,

            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,

            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                           | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,

            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        }
    };


    FxSizedArray attachments = {
        // Albedo attachment
        VkAttachmentDescription {
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        },
    };

    VkRenderPassCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = static_cast<uint32>(attachments.Size),
        .pAttachments = attachments.Data,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = sizeof(subpass_dependencies) / sizeof(subpass_dependencies[0]),
        .pDependencies = subpass_dependencies,
    };

    const VkResult status = vkCreateRenderPass(device.Device, &create_info, nullptr, &RenderPass);
    if (status != VK_SUCCESS) {
        FxModulePanic("Failed to create render pass", status);
    }
}

void RvkRenderPass::Begin()
{
    if (RenderPass == nullptr) {
        FxModulePanic("Render pass has not been previously created", 0);
    }

    RvkFrameData *frame = Renderer->GetFrame();
    const auto extent = Renderer->Swapchain.Extent;

    //Log::Debug("Amount of framebuffers: %d", renderer->Swapchain.Framebuffers.Size);

    const VkClearValue clear_values[] = {
        // Albedo
        VkClearValue {
            .color = { { 1.0f, 0.8f, 0.7f, 1.0f } }
        },
        // Positions
        VkClearValue {
            .color = { { 0.0f, 0.0f, 0.0f, 0.0f } }
        },
        VkClearValue {
            .depthStencil = { 1.0f, 0 }
        }
    };
    VkRenderPassBeginInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = RenderPass,
        .framebuffer = Renderer->Swapchain.GPassFramebuffers[Renderer->GetImageIndex()].Framebuffer,
        .renderArea.offset = {0, 0},
        .renderArea.extent = {(uint32)extent.Width(), (uint32)extent.Height()},
        .clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]),
        .pClearValues = clear_values,
    };


    CommandBuffer = &frame->CommandBuffer;
    vkCmdBeginRenderPass(CommandBuffer->CommandBuffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
}

void RvkRenderPass::BeginComp()
{
    if (RenderPass == nullptr) {
        FxModulePanic("Render pass has not been previously created", 0);
    }

    RvkFrameData *frame = Renderer->GetFrame();
    const auto extent = Renderer->Swapchain.Extent;

    //Log::Debug("Amount of framebuffers: %d", renderer->Swapchain.Framebuffers.Size);

    const VkClearValue clear_values[] = {
        // Albedo
        VkClearValue {
            .color = { { 1.0f, 0.8f, 0.7f, 1.0f } }
        },
    };
    VkRenderPassBeginInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = RenderPass,
        .framebuffer = Renderer->Swapchain.CompFramebuffers[Renderer->GetImageIndex()].Framebuffer,
        .renderArea.offset = {0, 0},
        .renderArea.extent = {(uint32)extent.Width(), (uint32)extent.Height()},
        .clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]),
        .pClearValues = clear_values,
    };


    CommandBuffer = &frame->CommandBuffer;
    vkCmdBeginRenderPass(CommandBuffer->CommandBuffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
}

void RvkRenderPass::End()
{
    if (CommandBuffer == VK_NULL_HANDLE) {
        throw std::runtime_error("Command buffer is null when ending render pass!");
    }

    vkCmdEndRenderPass(CommandBuffer->CommandBuffer);
}

void RvkRenderPass::Destroy(RvkGpuDevice &device)
{
    if (RenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device.Device, RenderPass, nullptr);
    }
}
