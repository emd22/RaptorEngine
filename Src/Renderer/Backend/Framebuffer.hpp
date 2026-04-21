#pragma once

#include "Math/Vec2.hpp"
#include "Pipeline.hpp"

#include <vulkan/vulkan.h>

#include <Core/SizedArray.hpp>

namespace fx::renderer {

class Framebuffer
{
public:
    void Create(const SizedArray<VkImageView>& image_views, const RenderPass& render_pass, Vec2u size);
    void Destroy();

    FX_FORCE_INLINE VkFramebuffer Get() const { return Framebuffer; }

    ~Framebuffer() { Destroy(); }

public:
    VkFramebuffer Framebuffer = nullptr;
    GpuDevice* mDevice = nullptr;
};

} // namespace fx::renderer
