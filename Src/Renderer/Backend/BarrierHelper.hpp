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
 * @brief Moves an image to the general layout for compute shader writes, discarding what it held.
 */
void ImageBeginComputeWrite(const CommandBuffer& cmd, Image* image);

/**
 * @brief Makes compute shader writes to an image visible to compute and fragment shader reads.
 */
void ImageComputeWriteToRead(const CommandBuffer& cmd, Image* image);

/**
 * @brief Moves an image that nothing has written yet to the shader read layout, so compute can sample it.
 */
void ImageBeginComputeRead(const CommandBuffer& cmd, Image* image);

/**
 * @brief Copies a compute written image (left in the shader read layout) into a history image that compute samples
 * next frame. The source is left in the transfer source layout.
 */
void ImageCopyToComputeHistory(const CommandBuffer& cmd, Image* source, Image* history);

/**
 * @brief Makes every earlier write in the command buffer visible to compute shader reads.
 */
void ComputeReadAfterAll(const CommandBuffer& cmd);

/**
 * @brief Makes storage buffer writes from a compute shader visible to the fragment shader.
 */
void BufferComputeToFragment(const CommandBuffer& cmd, RawGpuBuffer* buffer);

/**
 * @brief Holds back compute shader writes to a buffer until the fragment shader reads recorded before it are done.
 */
void BufferFragmentToCompute(const CommandBuffer& cmd, RawGpuBuffer* buffer);

} // namespace BarrierHelper
} // namespace renderer
} // namespace fx
