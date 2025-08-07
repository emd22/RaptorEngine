#pragma once

#include <vulkan/vulkan.h>

#include "Math/Vec2.hpp"
#include "RxPipeline.hpp"

#include <Core/FxSizedArray.hpp>


class RxFramebuffer
{
public:
    void Create(const FxSizedArray<VkImageView> &image_views, const RxGraphicsPipeline &pipeline, FxVec2u size);
    void Destroy();

    ~RxFramebuffer()
    {
        Destroy();
    }

public:
    VkFramebuffer Framebuffer = nullptr;
    RxGpuDevice *mDevice = nullptr;
};
