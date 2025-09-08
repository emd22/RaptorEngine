#include "Rx_Fwd_AddToDeletionQueue.hpp"
#include "Rx_Fwd_GetDevice.hpp"
#include "Rx_Fwd_GetFrame.hpp"
#include "Rx_Fwd_GetGpuAllocator.hpp"
#include "Rx_Fwd_SubmitUploadGpuCmd.hpp"

#include <Renderer/Renderer.hpp>

/* Rx_Fwd_GetFrame.hpp */
RxFrameData* Rx_Fwd_GetFrame() { return Renderer->GetFrame(); }

/* Rx_Fwd_SubmitUploadGpuCmd.hpp */
void Fx_Fwd_SubmitUploadCmd(std::function<void(RxCommandBuffer&)> func) { Renderer->SubmitUploadCmd(func); }


VmaAllocator Fx_Fwd_GetGpuAllocator() { return Renderer->GpuAllocator; }

RxGpuDevice* Fx_Fwd_GetGpuDevice() { return Renderer->GetDevice(); }


void Fx_Fwd_AddToDeletionQueue(const FxDeletionObject::FuncType& func) { Renderer->AddToDeletionQueue(func); }

void Fx_Fwd_AddGpuBufferToDeletionQueue(const VkBuffer& buffer, const VmaAllocation& allocation)
{
    Renderer->AddGpuBufferToDeletionQueue(buffer, allocation);
}
