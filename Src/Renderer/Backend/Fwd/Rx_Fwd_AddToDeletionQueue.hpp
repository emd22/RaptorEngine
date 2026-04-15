#pragma once

#include <ThirdParty/vk_mem_alloc.h>

#include <Renderer/DeletionObject.hpp>

namespace fx::renderer {

void Fx_Fwd_AddToDeletionQueue(const DeletionObject::FuncType& func);
void Fx_Fwd_AddGpuBufferToDeletionQueue(const VkBuffer& buffer, const VmaAllocation& allocation);

} // namespace fx::renderer
