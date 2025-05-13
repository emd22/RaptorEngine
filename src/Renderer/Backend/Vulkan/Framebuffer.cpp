#include "Framebuffer.hpp"
#include "Core/Defines.hpp"
#include <Core/FxPanic.hpp>
#include "Renderer/Backend/Vulkan/Device.hpp"
#include "Renderer/Renderer.hpp"
#include <Core/Types.hpp>
#include "vulkan/vulkan_core.h"

namespace vulkan {

FX_SET_MODULE_NAME("Framebuffer")

void Framebuffer::Create(StaticArray<VkImageView> &image_views, GraphicsPipeline &pipeline, Vec2u size)
{
    AssertRendererExists();

    const VkFramebufferCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = pipeline.RenderPass.RenderPass,
        .attachmentCount = (uint32)image_views.Size,
        .pAttachments = image_views.Data,
        .width = size.X,
        .height = size.Y,
        .layers = 1,
    };

    mDevice = RendererVulkan->GetDevice();

    const VkResult status = vkCreateFramebuffer(mDevice->Device, &create_info, nullptr, &Framebuffer);

    if (status != VK_SUCCESS) {
        FxPanic("Failed to create framebuffer", status);
    }
}

void Framebuffer::Destroy()
{
    vkDestroyFramebuffer(mDevice->Device, Framebuffer, nullptr);
}


}; // namespace vulkan
