#pragma once

#include <vulkan/vulkan.h>

#include <Core/Types.hpp>

namespace fx {
class Image;

namespace renderer {
class CommandBuffer;

namespace BarrierHelper {

void ImageTransferHandoff(const CommandBuffer& cmd, Image* image);
void ImageGraphicsAcquire(const CommandBuffer& cmd, Image* image);

void ImageLayoutTransition(Image* image, VkImageLayout new_layout, CommandBuffer& cmd, uint32 mip_level,
						   uint32 num_levels);

} // namespace BarrierHelper
} // namespace renderer
} // namespace fx
