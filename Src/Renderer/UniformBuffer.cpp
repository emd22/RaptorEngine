#include "UniformBuffer.hpp"

#include "Constants.hpp"
#include "RenderBackend.hpp"

#include <Renderer/Globals.hpp>

namespace fx::renderer {

void Uniforms::Create(uint32 slot_size, uint32 count)
{
    SlotSize = slot_size;
    Count = count;

    PageSize = (SlotSize * Count);

    uint32 size_in_frames = PageSize * FramesInFlight;

    mGpuBuffer.Create(eGpuBufferType::UniformWithOffset, size_in_frames, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                      eGpuBufferFlags::PersistentMapped);
}

void Uniforms::Rewind()
{
    mUniformIndex = 0;
    SlotIndex = 0;
}

uint32 Uniforms::GetBaseOffset() const { return PageSize * gRenderer->GetFrameNumber(); }
uint32 Uniforms::GetBaseOffset(uint32 frame_index) const { return PageSize * frame_index; }

uint32 Uniforms::GetSlotOffset() const { return SlotIndex * SlotSize; }

void Uniforms::SetAllValuesRaw(const void* data, uint32 value_size, bool all_frames)
{
    Assert((PageSize % value_size) == 0);

    uint32 num_values = PageSize / value_size;

    // Allocate a temporary buffer to copy once and reduce the amount of flushes to the GPU buffer
    void* tmp_buffer = gEnginePool->AllocRaw(PageSize);

    for (uint32 index = 0; index < num_values; index++) {
        // Copy to the offset of the value for the current index
        void* dst = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(tmp_buffer) + (value_size * index));
        memcpy(dst, data, value_size);
    }

    uint32 frame_count = (all_frames ? FramesInFlight : 1);

    // Copy the temp buffer for each frame
    for (uint32 frame_index = 0; frame_index < frame_count; frame_index++) {
        uint8* dst = GetBasePtr() + GetBaseOffset(frame_index);
        memcpy(dst, tmp_buffer, PageSize);
    }

    LogInfo("Writing {} values to {} frames for uniform buffer", num_values, frame_count);

    gEnginePool->FreeRaw(tmp_buffer);
}

uint8* Uniforms::GetBasePtr() { return reinterpret_cast<uint8*>(mGpuBuffer.pMappedBuffer); }

} // namespace fx::renderer
