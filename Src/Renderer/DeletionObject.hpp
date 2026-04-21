#pragma once

#include <ThirdParty/vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <Core/Types.hpp>
#include <functional>

namespace fx {

struct DeletionObject
{
    using FuncType = std::function<void(DeletionObject*)>;
    // using FuncType = void (*)(DeletionObject *object);

    VkBuffer Buffer = VK_NULL_HANDLE;
    VmaAllocation Allocation = VK_NULL_HANDLE;

    uint32 DeletionFrameNumber = 0;
    FuncType Func = [](DeletionObject* object) {};

    bool bIsGpuBuffer : 1 = false;
};

} // namespace fx
