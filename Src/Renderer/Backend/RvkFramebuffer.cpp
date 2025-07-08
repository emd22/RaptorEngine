#include "RvkFramebuffer.hpp"
#include "RvkDevice.hpp"
#include "../Renderer.hpp"

#include <Core/Defines.hpp>
#include <Core/FxPanic.hpp>
#include <Core/Types.hpp>

FX_SET_MODULE_NAME("Framebuffer")

void RvkFramebuffer::Create(const FxSizedArray<VkImageView> &image_views, const RvkGraphicsPipeline &pipeline, Vec2u size)
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

    mDevice = Renderer->GetDevice();

    const VkResult status = vkCreateFramebuffer(mDevice->Device, &create_info, nullptr, &Framebuffer);

    if (status != VK_SUCCESS) {
        FxModulePanic("Failed to create framebuffer", status);
    }
}

void RvkFramebuffer::Destroy()
{
    vkDestroyFramebuffer(mDevice->Device, Framebuffer, nullptr);
}
