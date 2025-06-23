#include "Fwd_AddToDeletionQueue.hpp"

#include <Renderer/Renderer.hpp>


void Fx_Fwd_AddToDeletionQueue(const FxDeletionObject::FuncType& func)
{
    Renderer->AddToDeletionQueue(func);
}

void Fx_Fwd_AddGpuBufferToDeletionQueue(const VkBuffer& buffer, const VmaAllocation& allocation)
{
    Renderer->AddGpuBufferToDeletionQueue(buffer, allocation);
}
