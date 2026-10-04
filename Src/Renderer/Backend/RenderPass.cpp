#include "RenderPass.hpp"

#include "Device.hpp"
#include "Swapchain.hpp"

#include <vulkan/vulkan.h>

#include <Core/Assert.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

FX_SET_MODULE_NAME("RenderPass")

namespace fx::renderer {

void RenderPass::Create(TargetList& attachments, Vec2u size, const Vec2u& offset)
{
	if (size == Target::scFullScreen) {
		size = gGraphics->Swapchain.Extent;
	}

	AttachmentCount = attachments.Targets.Size;

	Size = size;
	Offset = offset;

	Assert(size.X > 0.0f && size.Y > 0.0f);

	mpDevice = gGraphics->GetDevice();

	SizedArray<VkAttachmentDescription>& descriptions = attachments.GetDescriptions();

	SizedArray<uint8> is_depth(attachments.Targets.Size);

	for (int i = 0; i < attachments.Targets.Size; i++) {
		is_depth.Insert(attachments.Targets[i].IsDepth() ? 1 : 0);
	}

	uint64 handle = 0;

	const VkResult status = static_cast<VkResult>(rx_gpu_render_pass_create(
		mpDevice->GetRustDevice(), descriptions.pData, is_depth.pData, descriptions.Size, &handle));

	if (status != VK_SUCCESS) {
		ModulePanicVulkan("Failed to create render pass", status);
	}

	InternalRenderPass = RxFromRaw<VkRenderPass>(handle);
}

void RenderPass::Begin(CommandBuffer* cmd, VkFramebuffer framebuffer, const Slice<VkClearValue>& clear_values)
{
	const VkRect2D render_area = {
		.offset = { .x = static_cast<int32>(Offset.X), .y = static_cast<int32>(Offset.Y) },
		.extent = { .width = Size.Width(), .height = Size.Height() },
	};

	Begin(cmd, framebuffer, clear_values, render_area);
}

void RenderPass::Begin(CommandBuffer* cmd, VkFramebuffer framebuffer, const Slice<VkClearValue>& clear_values,
					   const VkRect2D& render_area)
{
	pCommandBuffer = cmd;

	if (InternalRenderPass == nullptr) {
		ModulePanic("Render pass has not been previously created", 0);
	}

	rx_gpu_cmd_begin_render_pass(mpDevice->GetRustDevice(), cmd->Cmd, RxRaw(InternalRenderPass), RxRaw(framebuffer),
								 render_area.offset.x, render_area.offset.y, render_area.extent.width,
								 render_area.extent.height, clear_values.pData, clear_values.Size);
}

void RenderPass::End()
{
	Assert(pCommandBuffer != nullptr);

	rx_gpu_cmd_end_render_pass(mpDevice->GetRustDevice(), pCommandBuffer->Cmd);
}

void RenderPass::Destroy()
{
	if (InternalRenderPass != nullptr) {
		rx_gpu_render_pass_destroy(mpDevice->GetRustDevice(), RxRaw(InternalRenderPass));
	}
	InternalRenderPass = nullptr;
}

} // namespace fx::renderer
