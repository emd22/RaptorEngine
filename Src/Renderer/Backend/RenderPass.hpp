#pragma once

#include <vulkan/vulkan.h>

#include <Core/Slice.hpp>
#include <Math/Vec2.hpp>
#include <Renderer/Target.hpp>

namespace fx::renderer {


class Swapchain;
class GpuDevice;
class CommandBuffer;

class RenderPass
{
public:
	void Create(TargetList& color_attachments, UVec2 size, const UVec2& offset = UVec2::scZero);

	void Begin(CommandBuffer* cmd, VkFramebuffer framebuffer, const Slice<VkClearValue>& clear_colors);

	/**
	 * @brief Begins the render pass over a section of the framebuffer.
	 */
	void Begin(CommandBuffer* cmd, VkFramebuffer framebuffer, const Slice<VkClearValue>& clear_colors,
			   const VkRect2D& render_area);
	void End();

	void Destroy();

	FX_FORCE_INLINE VkRenderPass Get() const { return InternalRenderPass; }

	~RenderPass() { Destroy(); }

public:
	VkRenderPass InternalRenderPass = nullptr;
	CommandBuffer* pCommandBuffer = nullptr;

	UVec2 Size = UVec2::scZero;
	UVec2 Offset = UVec2::scZero;

	uint32 AttachmentCount = 0;

private:
	GpuDevice* mpDevice = nullptr;
};

} // namespace fx::renderer
