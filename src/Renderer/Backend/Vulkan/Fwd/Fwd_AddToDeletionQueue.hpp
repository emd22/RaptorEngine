#pragma once

#include <Renderer/Backend/Vulkan/FxDeletionObject.hpp>

namespace vulkan {

void Fx_Fwd_AddToDeletionQueue(FxDeletionObject::FuncType func);
void Fx_Fwd_AddGpuBufferToDeletionQueue(VkBuffer &buffer, VmaAllocation &allocation);

}
