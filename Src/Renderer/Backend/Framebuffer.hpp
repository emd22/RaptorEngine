#pragma once

#include "Math/Vec2.hpp"

#include <vulkan/vulkan.h>

#include <Core/SizedArray.hpp>

namespace fx::renderer {

class RenderPass;

class Framebuffer
{
public:
    void Create(const SizedArray<VkImageView>& image_views, const RenderPass& render_pass, Vec2u size);
    void Destroy();

    FX_FORCE_INLINE VkFramebuffer Get() const { return InternalFramebuffer; }

    ~Framebuffer() { Destroy(); }

public:
    VkFramebuffer InternalFramebuffer = nullptr;
};

} // namespace fx::renderer
