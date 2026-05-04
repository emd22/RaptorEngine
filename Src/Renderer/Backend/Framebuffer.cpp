#include "Framebuffer.hpp"

#include "Device.hpp"
#include "RenderPass.hpp"

#include <Core/Defines.hpp>
#include <Core/Panic.hpp>
#include <Core/Types.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx::renderer {

FX_SET_MODULE_NAME("Framebuffer")

void Framebuffer::Create(const SizedArray<VkImageView>& image_views, const RenderPass& render_pass, Vec2u size)
{
    if (size == Target::scFullScreen) {
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

    const VkResult status = vkCreateFramebuffer(gRenderer->GetDevice()->Device, &create_info, nullptr, &Framebuffer);

    if (status != VK_SUCCESS) {
        ModulePanicVulkan("Failed to create framebuffer", status);
    }
}

void Framebuffer::Destroy()
{
    if (!Framebuffer) {
        return;
    }

    vkDestroyFramebuffer(gRenderer->GetDevice()->Device, Framebuffer, nullptr);
    Framebuffer = nullptr;
}

} // namespace fx::renderer
