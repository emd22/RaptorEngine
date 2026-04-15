#include "RxFramebuffer.hpp"

#include "RxDevice.hpp"

#include <Core/Defines.hpp>
#include <Core/Panic.hpp>
#include <Core/Types.hpp>
#include <Renderer/RxGlobals.hpp>
#include <Renderer/RxRenderBackend.hpp>

namespace fx::renderer {

FX_SET_MODULE_NAME("Framebuffer")

void RxFramebuffer::Create(const SizedArray<VkImageView>& image_views, const RxRenderPass& render_pass, Vec2u size)
{
    if (size == RxTarget::scFullScreen) {
        size = gRenderer->Swapchain.Extent;
    }

    const VkFramebufferCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = render_pass.RenderPass,
        .attachmentCount = static_cast<uint32>(image_views.Size),
        .pAttachments = image_views.pData,
        .width = size.X,
        .height = size.Y,
        .layers = 1,
    };

    mDevice = gRenderer->GetDevice();

    const VkResult status = vkCreateFramebuffer(mDevice->Device, &create_info, nullptr, &Framebuffer);

    if (status != VK_SUCCESS) {
        ModulePanicVulkan("Failed to create framebuffer", status);
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

} // namespace fx::renderer
