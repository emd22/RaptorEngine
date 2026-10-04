/*
 * File:        BarrierHelper.cpp
 * Author:      emd22
 * Created:     10/08/2026
 * Description: Helper functions for creating barriers for images and buffers
 */

#include "BarrierHelper.hpp"

#include "Image.hpp"

#include <Core/Assert.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

namespace fx::renderer {
namespace BarrierHelper {

void ImageTransferHandoff(const CommandBuffer& cmd, Image* image)
{
	Assert(image != nullptr);

	const int32 recorded = rx_gpu_cmd_image_transfer_release(
		gGraphics->GetDevice()->GetRustDevice(), cmd.Cmd, RxRaw(image->InternalImage),
		ImageFormatUtil::GetAspectMask(image->Info.Format), image->Info.MipCount);

	if (recorded != 0) {
		image->ImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}
}

void ImageGraphicsAcquire(const CommandBuffer& cmd, Image* image)
{
	Assert(image != nullptr);

	const int32 recorded = rx_gpu_cmd_image_graphics_acquire(
		gGraphics->GetDevice()->GetRustDevice(), cmd.Cmd, RxRaw(image->InternalImage),
		ImageFormatUtil::GetAspectMask(image->Info.Format), image->Info.MipCount);

	if (recorded != 0) {
		image->ImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}
}

void ImageLayoutTransition(Image* image, VkImageLayout new_layout, CommandBuffer& cmd, uint32 mip_level,
						   uint32 num_levels)
{
	rx_gpu_cmd_image_layout_transition(gGraphics->GetDevice()->GetRustDevice(), cmd.Get(),
									   RxRaw(image->InternalImage),
									   ImageFormatUtil::GetAspectMask(image->Info.Format), image->ImageLayout,
									   new_layout, mip_level, num_levels, cmd.QueueFamily());

	image->ImageLayout = new_layout;
}

void BufferComputeToFragment(const CommandBuffer& cmd, RawGpuBuffer* buffer)
{
	Assert(buffer != nullptr);

	rx_gpu_cmd_buffer_compute_to_fragment(gGraphics->GetDevice()->GetRustDevice(), cmd.Cmd, RxRaw(buffer->Buffer),
										  buffer->Size);
}

void BufferFragmentToCompute(const CommandBuffer& cmd, RawGpuBuffer* buffer)
{
	Assert(buffer != nullptr);

	rx_gpu_cmd_buffer_fragment_to_compute(gGraphics->GetDevice()->GetRustDevice(), cmd.Cmd, RxRaw(buffer->Buffer),
										  buffer->Size);
}

} // namespace BarrierHelper
} // namespace fx::renderer
