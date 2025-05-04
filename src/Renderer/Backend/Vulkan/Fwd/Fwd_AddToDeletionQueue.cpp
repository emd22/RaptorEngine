#include "Fwd_AddToDeletionQueue.hpp"

#include <Renderer/Renderer.hpp>

namespace vulkan {

void Fx_Fwd_AddToDeletionQueue(FxDeletionObject::FuncType func)
{
    Renderer->AddToDeletionQueue(func);
}

void Fx_Fwd_AddGpuBufferToDeletionQueue(VkBuffer &buffer, VmaAllocation &allocation)
{
    Renderer->AddGpuBufferToDeletionQueue(buffer, allocation);
}

}
