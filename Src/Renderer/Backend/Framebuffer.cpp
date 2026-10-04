#include "Framebuffer.hpp"

#include "Device.hpp"
#include "RenderPass.hpp"

#include <Core/Assert.hpp>
#include <Core/Defines.hpp>
#include <Core/Types.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

namespace fx::renderer {

FX_SET_MODULE_NAME("Framebuffer")

void Framebuffer::Create(const SizedArray<VkImageView>& image_views, const RenderPass& render_pass, Vec2u size)
{
	Assert(render_pass.Get() != nullptr);
	Assert(image_views.Size > 0);

	if (size == Target::scFullScreen) {
		size = gGraphics->Swapchain.Extent;
	}

	static_assert(sizeof(VkImageView) == sizeof(uint64));

	uint64 handle = 0;

	const VkResult status = static_cast<VkResult>(rx_gpu_framebuffer_create(
		gGraphics->GetDevice()->GetRustDevice(), RxRaw(render_pass.Get()),
		reinterpret_cast<const uint64*>(image_views.pData), image_views.Size, size.X, size.Y, &handle));

	if (status != VK_SUCCESS) {
		ModulePanicVulkan("Failed to create framebuffer", status);
	}

	InternalFramebuffer = RxFromRaw<VkFramebuffer>(handle);
}

void Framebuffer::Destroy()
{
	if (!InternalFramebuffer) {
		return;
	}

	rx_gpu_framebuffer_destroy(gGraphics->GetDevice()->GetRustDevice(), RxRaw(InternalFramebuffer));
	InternalFramebuffer = nullptr;
}

} // namespace fx::renderer
