#pragma once

#include <vulkan/vulkan.h>

#include <Core/Slice.hpp>
#include <Math/Vec2.hpp>
#include <Renderer/RxAttachment.hpp>

namespace fx::renderer {


class RxSwapchain;
class RxGpuDevice;
class RxCommandBuffer;

class RxRenderPass
{
public:
    void Create(RxTargetList& color_attachments, Vec2u size, const Vec2u& offset = Vec2u::sZero);

    void Begin(RxCommandBuffer* cmd, VkFramebuffer framebuffer, const Slice<VkClearValue>& clear_colors);
    void End();

    void Destroy();

    FX_FORCE_INLINE VkRenderPass Get() const { return RenderPass; }

    ~RxRenderPass() { Destroy(); }

public:
    VkRenderPass RenderPass = nullptr;
    RxCommandBuffer* pCommandBuffer = nullptr;

    Vec2u Size = Vec2u::sZero;
    Vec2u Offset = Vec2u::sZero;

private:
    RxGpuDevice* mpDevice = nullptr;
};

} // namespace fx::renderer
