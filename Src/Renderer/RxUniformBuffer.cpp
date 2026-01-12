#include "RxUniformBuffer.hpp"

#include "RxConstants.hpp"

void RxUniforms::Create()
{
    static_assert((scUniformBufferSize % 2 == 0));
    constexpr uint32 size_in_ints = (scUniformBufferSize / sizeof(uint32)) * RxFramesInFlight;

    mGpuBuffer.Create(size_in_ints, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
                      RxGpuBufferFlags::ePersistentMapped);
}
