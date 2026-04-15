#include "RxUniformBuffer.hpp"

#include "RxConstants.hpp"
#include "RxRenderBackend.hpp"

#include <Renderer/RxGlobals.hpp>

namespace fx::renderer {

void RxUniforms::Create(uint32 size)
{
    Size = size;
    uint32 size_in_frames = Size * RxFramesInFlight;

    mGpuBuffer.Create(RxGpuBufferType::eUniformWithOffset, size_in_frames, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                      RxGpuBufferFlags::ePersistentMapped);
}

void RxUniforms::Rewind() { mUniformIndex = 0; }


uint32 RxUniforms::GetBaseOffset() const { return Size * gRenderer->GetFrameNumber(); }
uint32 RxUniforms::GetBaseOffset(uint32 frame_index) const { return Size * frame_index; }

void RxUniforms::SetAllValuesRaw(const void* data, uint32 value_size, bool all_frames)
{
    Assert((Size % value_size) == 0);

    uint32 num_values = Size / value_size;

    // Allocate a temporary buffer to copy once and reduce the amount of flushes to the GPU buffer
    void* tmp_buffer = gEnginePool->AllocRaw(Size);

    for (uint32 index = 0; index < num_values; index++) {
        // Copy to the offset of the value for the current index
        void* dst = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(tmp_buffer) + (value_size * index));
        memcpy(dst, data, value_size);
    }

    uint32 frame_count = (all_frames ? RxFramesInFlight : 1);

    // Copy the temp buffer for each frame
    for (uint32 frame_index = 0; frame_index < frame_count; frame_index++) {
        uint8* dst = GetBasePtr() + GetBaseOffset(frame_index);
        memcpy(dst, tmp_buffer, Size);
    }

    LogInfo("Writing {} values to {} frames for uniform buffer", num_values, frame_count);

    gEnginePool->FreeRaw(tmp_buffer);
}

uint8* RxUniforms::GetBasePtr() { return reinterpret_cast<uint8*>(mGpuBuffer.pMappedBuffer); }

} // namespace fx::renderer
