use ash::vk::{self, Handle};
use raptor_gfx::{CommandBuffer, Gfx, GpuBuffer};
use raptor_gpu::ImageRecord;

pub fn transition_raw(
	gfx: &Gfx,
	cmd: vk::CommandBuffer,
	queue_family: u32,
	image: &ImageRecord,
	new_layout: vk::ImageLayout,
	base_mip: u32,
	levels: u32,
) {
	let fields = image.fields();

	// SAFETY: the command buffer is recording and the image is live, as the caller guarantees.
	unsafe {
		gfx.device().cmd_image_layout_transition(
			cmd,
			&raptor_gpu::LayoutTransition {
				image: vk::Image::from_raw(fields.image),
				aspect: vk::ImageAspectFlags::from_raw(fields.aspect),
				old_layout: vk::ImageLayout::from_raw(fields.layout),
				new_layout,
				base_mip,
				levels,
				cmd_queue_family: queue_family,
			},
		)
	};

	image.set_layout(new_layout);
}

pub fn transition_record(
	gfx: &Gfx,
	cmd: &CommandBuffer,
	image: &ImageRecord,
	new_layout: vk::ImageLayout,
	base_mip: u32,
	levels: u32,
) {
	transition_raw(
		gfx,
		cmd.raw(),
		cmd.queue_family(),
		image,
		new_layout,
		base_mip,
		levels,
	);
}

pub fn write_buffer_range(buffer: &GpuBuffer, offset: u64, data: &[u8]) -> bool {
	let mapped = buffer.mapped_ptr();

	if mapped.is_null() || offset + data.len() as u64 > buffer.size() {
		return false;
	}

	// SAFETY: the buffer is mapped and the range was checked to lie inside it.
	unsafe {
		std::ptr::copy_nonoverlapping(data.as_ptr(), mapped.add(offset as usize), data.len())
	};

	let _ = buffer.flush(offset, data.len() as u64);

	true
}
