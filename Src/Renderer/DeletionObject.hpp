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

    uint32 DeletionFrameNumber = 0;
    FuncType Func = [](DeletionObject* object) {};
};

} // namespace fx
