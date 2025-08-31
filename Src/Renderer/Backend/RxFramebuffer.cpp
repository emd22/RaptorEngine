#include "RxFramebuffer.hpp"

#include "../Renderer.hpp"
#include "RxDevice.hpp"

#include <Core/FxDefines.hpp>
#include <Core/FxPanic.hpp>
#include <Core/Types.hpp>

FX_SET_MODULE_NAME("Framebuffer")

void RxFramebuffer::Create(const FxSizedArray<VkImageView>& image_views, const RxGraphicsPipeline& pipeline,
                           const RxRenderPass& render_pass, FxVec2u size)
{
    AssertRendererExists();

    const VkFramebufferCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = render_pass.RenderPass,
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

void RxFramebuffer::Destroy()
{
    if (!Framebuffer) {
        return;
    }
    vkDestroyFramebuffer(mDevice->Device, Framebuffer, nullptr);
    Framebuffer = nullptr;
}
