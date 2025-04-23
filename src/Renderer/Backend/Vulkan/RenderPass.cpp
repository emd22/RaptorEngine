#include <vulkan/vulkan.h>
#include <stdexcept>

#include <Renderer/Renderer.hpp>

#include "Device.hpp"
#include "RenderPass.hpp"

#include "Swapchain.hpp"

#include <Core/FxPanic.hpp>

namespace vulkan {

FX_SET_MODULE_NAME("RenderPass")

void RenderPass::Create(GPUDevice &device, Swapchain &swapchain) {
    VkAttachmentDescription color_attachment = {
        .format = swapchain.SurfaceFormat.format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };


    VkAttachmentReference color_attachment_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_ref,
    };


    VkSubpassDependency subpassDependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,

    };

    VkRenderPassCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &subpassDependency,
    };

    const VkResult status = vkCreateRenderPass(device.Device, &create_info, nullptr, &RenderPass);
    if (status != VK_SUCCESS) {
        FxPanic("Failed to create render pass", status);
    }
}

void RenderPass::Begin() {
    if (RenderPass == nullptr) {
        FxPanic("Render pass has not been previously created", 0);
    }

    VkClearValue clear_color = {};
    clear_color.color = {{1.0f, 1.0f, 1.0f, 1.0f}};

    FrameData *frame = RendererVulkan->GetFrame();
    const auto extent = RendererVulkan->Swapchain.Extent;

    //Log::Debug("Amount of framebuffers: %d", renderer->Swapchain.Framebuffers.Size);

    VkRenderPassBeginInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = RenderPass,
        .framebuffer = RendererVulkan->Swapchain.Framebuffers[RendererVulkan->GetImageIndex()].Framebuffer,
        .renderArea.offset = {0, 0},
        .renderArea.extent = {(uint32)extent.Width(), (uint32)extent.Height()},
        .clearValueCount = 1,
        .pClearValues = &clear_color,
    };


    CommandBuffer = &frame->CommandBuffer;
    vkCmdBeginRenderPass(CommandBuffer->CommandBuffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
}

void RenderPass::End() {
    if (CommandBuffer == VK_NULL_HANDLE) {
        throw std::runtime_error("Command buffer is null when ending render pass!");
    }

    vkCmdEndRenderPass(CommandBuffer->CommandBuffer);
}

void RenderPass::Destroy(GPUDevice &device) {
    if (RenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device.Device, RenderPass, nullptr);
    }
}

}; // namespace vulkan
