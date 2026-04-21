#pragma once

#include <vulkan/vulkan.h>

#include <Core/Slice.hpp>
#include <Math/Vec2.hpp>
#include <Renderer/Attachment.hpp>

namespace fx::renderer {


class Swapchain;
class GpuDevice;
class CommandBuffer;

class RenderPass
{
public:
    void Create(TargetList& color_attachments, Vec2u size, const Vec2u& offset = Vec2u::sZero);

    void Begin(CommandBuffer* cmd, VkFramebuffer framebuffer, const Slice<VkClearValue>& clear_colors);
    void End();

    void Destroy();

    FX_FORCE_INLINE VkRenderPass Get() const { return RenderPass; }

    ~RenderPass() { Destroy(); }

public:
    VkRenderPass RenderPass = nullptr;
    CommandBuffer* pCommandBuffer = nullptr;

    Vec2u Size = Vec2u::sZero;
    Vec2u Offset = Vec2u::sZero;

private:
    GpuDevice* mpDevice = nullptr;
};

} // namespace fx::renderer
