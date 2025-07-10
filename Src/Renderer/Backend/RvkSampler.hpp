#pragma once

#include <vulkan/vulkan.h>

#include <Core/Log.hpp>

#include <Renderer/Backend/RvkDevice.hpp>

class RvkSampler
{
public:

    void Create();
    void Destroy();

    ~RvkSampler()
    {
        Destroy();
    }

public:
    VkSampler Sampler;

private:
    RvkGpuDevice* mDevice = nullptr;
};
