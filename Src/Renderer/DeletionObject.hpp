#pragma once

#include <raptor_ffi.h>
#include <vulkan/vulkan.h>

#include <Core/Types.hpp>
#include <functional>

namespace fx {

struct DeletionObject
{
    using FuncType = std::function<void(DeletionObject*)>;
    // using FuncType = void (*)(DeletionObject *object);

    VkBuffer Buffer = VK_NULL_HANDLE;
    RxGpuAllocation* Allocation = nullptr;

    uint32 DeletionFrameNumber = 0;
    FuncType Func = [](DeletionObject* object) {};

    bool bIsGpuBuffer : 1 = false;
};

} // namespace fx
