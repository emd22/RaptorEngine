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
    void Create(TargetList& color_attachments, Vec2u size, const Vec2u& offset = Vec2u::sZero);

    void Begin(CommandBuffer* cmd, VkFramebuffer framebuffer, const Slice<VkClearValue>& clear_colors);

    /**
     * @brief Begins the render pass over part of the framebuffer. Load, clear and store ops only touch the pixels
     * inside of `render_area`, everything outside of it keeps its contents.
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

    Vec2u Size = Vec2u::sZero;
    Vec2u Offset = Vec2u::sZero;

    uint32 AttachmentCount = 0;

private:
    GpuDevice* mpDevice = nullptr;
};

} // namespace fx::renderer
