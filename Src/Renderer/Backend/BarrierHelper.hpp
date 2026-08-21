#pragma once

#include <vulkan/vulkan.h>

#include <Core/Types.hpp>

namespace fx {
class Image;

namespace renderer {
class CommandBuffer;
class RawGpuBuffer;

namespace BarrierHelper {

void ImageTransferHandoff(const CommandBuffer& cmd, Image* image);
void ImageGraphicsAcquire(const CommandBuffer& cmd, Image* image);

void ImageLayoutTransition(Image* image, VkImageLayout new_layout, CommandBuffer& cmd, uint32 mip_level,
						   uint32 num_levels);

/**
 * @brief Makes storage buffer writes from a compute shader visible to the fragment shader.
 */
void BufferComputeToFragment(const CommandBuffer& cmd, RawGpuBuffer* buffer);

} // namespace BarrierHelper
} // namespace renderer
} // namespace fx
