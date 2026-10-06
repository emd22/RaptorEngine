use ash::vk;
use raptor_gpu::{Device, LayoutTransition};

use crate::buffer::GpuBuffer;
use crate::image::Image;

pub fn transition_image(
	device: &Device,
	cmd: vk::CommandBuffer,
	queue_family: u32,
	image: &Image,
	new_layout: vk::ImageLayout,
	base_mip: u32,
	levels: u32,
) {
	// SAFETY: the command buffer is recording and the image is live, as the caller guarantees.
	unsafe {
		device.cmd_image_layout_transition(
			cmd,
			&LayoutTransition {
				image: image.handle(),
				aspect: image.format().aspect_mask(),
				old_layout: image.layout(),
				new_layout,
				base_mip,
				levels,
				cmd_queue_family: queue_family,
			},
		)
	};

	image.set_layout(new_layout);
}

pub fn buffer_compute_to_fragment(device: &Device, cmd: vk::CommandBuffer, buffer: &GpuBuffer) {
	// SAFETY: the command buffer is recording and the buffer is live.
	unsafe { device.cmd_buffer_compute_to_fragment(cmd, buffer.handle(), buffer.size()) };
}

pub fn buffer_fragment_to_compute(device: &Device, cmd: vk::CommandBuffer, buffer: &GpuBuffer) {
	// SAFETY: the command buffer is recording and the buffer is live.
	unsafe { device.cmd_buffer_fragment_to_compute(cmd, buffer.handle(), buffer.size()) };
}
