#pragma once

#include <vulkan/vulkan.h>

#include <Core/Types.hpp>

#include <functional>

struct FxDeletionObject
{
    using FuncType = std::function<void (FxDeletionObject*)>;
    // using FuncType = void (*)(FxDeletionObject *object);

    VkBuffer Buffer = VK_NULL_HANDLE;
    VmaAllocation Allocation = VK_NULL_HANDLE;

    uint32 DeletionFrameNumber = 0;
    FuncType Func = [](FxDeletionObject *object) { };

    bool IsGpuBuffer : 1 = false;
};
