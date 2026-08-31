#include "Fwd_AddToDeletionQueue.hpp"
#include "Fwd_GetDevice.hpp"
#include "Fwd_GetFrame.hpp"
#include "Fwd_GetGpuAllocator.hpp"
#include "Fwd_SubmitUploadGpuCmd.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

namespace fx::renderer {

/* Fwd_GetFrame.hpp */
FrameData* Fwd_GetFrame() { return gGraphics->GetFrame(); }

/* Fwd_SubmitUploadGpuCmd.hpp */
void Fx_Fwd_SubmitUploadCmd(std::function<void(CommandBuffer&)> func) { gGraphics->SubmitUploadCmd(func); }


VmaAllocator Fx_Fwd_GetGpuAllocator() { return gGraphics->GpuAllocator; }

GpuDevice* Fwd_GetDevice() { return gGraphics->GetDevice(); }

// void Fx_Fwd_AddToDeletionQueue(const DeletionObject::FuncType& func) { gGraphics->AddToDeletionQueue(func); }

// void Fx_Fwd_AddGpuBufferToDeletionQueue(const VkBuffer& buffer, const VmaAllocation& allocation)
// {
// 	gGraphics->AddGpuBufferToDeletionQueue(buffer, allocation);
// }

} // namespace fx::renderer
