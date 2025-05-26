#pragma once

#include "Core/FxStaticArray.hpp"
#include "Math/Vec2.hpp"
#include "Renderer/Backend/Vulkan/RvkDevice.hpp"
#include "Renderer/Backend/Vulkan/RvkPipeline.hpp"
#include <vulkan/vulkan.h>

namespace vulkan {

class RvkFramebuffer
{
public:
    void Create(FxStaticArray<VkImageView> &image_views, RvkGraphicsPipeline &pipeline, Vec2u size);
    void Destroy();

public:
    VkFramebuffer Framebuffer = nullptr;
    RvkGpuDevice *mDevice = nullptr;
};

}; // namespace vulkan
