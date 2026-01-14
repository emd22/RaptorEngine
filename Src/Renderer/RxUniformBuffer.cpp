#include "RxUniformBuffer.hpp"

#include "RxConstants.hpp"
#include "RxRenderBackend.hpp"

#include <FxEngine.hpp>

void RxUniforms::Create()
{
    static_assert((scUniformBufferSize % 2 == 0));
    constexpr uint32 size_in_ints = (scUniformBufferSize)*RxFramesInFlight;

    mGpuBuffer.Create(size_in_ints, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
                      RxGpuBufferFlags::ePersistentMapped);
}

void RxUniforms::Rewind() { mUniformIndex = 0; }


uint32 RxUniforms::GetBaseOffset() const { return scUniformBufferSize * gRenderer->GetFrameNumber(); }

uint8* RxUniforms::GetCurrentBuffer()
{
    // Offset by the frame currently in flight
    return reinterpret_cast<uint8*>(mGpuBuffer.pMappedBuffer) + GetBaseOffset();
}
