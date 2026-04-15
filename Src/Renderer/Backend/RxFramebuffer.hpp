#pragma once

#include "Math/Vec2.hpp"
#include "RxPipeline.hpp"

#include <vulkan/vulkan.h>

#include <Core/SizedArray.hpp>

namespace fx::renderer {

class RxFramebuffer
{
public:
    void Create(const SizedArray<VkImageView>& image_views, const RxRenderPass& render_pass, Vec2u size);
    void Destroy();

    FX_FORCE_INLINE VkFramebuffer Get() const { return Framebuffer; }

    ~RxFramebuffer() { Destroy(); }

public:
    VkFramebuffer Framebuffer = nullptr;
    RxGpuDevice* mDevice = nullptr;
};

} // namespace fx::renderer
