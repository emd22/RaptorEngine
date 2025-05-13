#pragma once

#include "Core/StaticArray.hpp"
#include "Math/Vec2.hpp"
#include "Renderer/Backend/Vulkan/Device.hpp"
#include "Renderer/Backend/Vulkan/Pipeline.hpp"
#include <vulkan/vulkan.h>

namespace vulkan {

class Framebuffer
{
public:
    void Create(StaticArray<VkImageView> &image_views, GraphicsPipeline &pipeline, Vec2u size);
    void Destroy();

public:
    VkFramebuffer Framebuffer = nullptr;
    RvkGpuDevice *mDevice = nullptr;
};

}; // namespace vulkan
