use ash::prelude::VkResult;
use ash::vk;

use crate::{BufferType, Device};

#[derive(Clone, Copy, Debug)]
pub enum DescriptorWrite
{
	Image
	{
		binding: u32,
		sampler: vk::Sampler,
		view: vk::ImageView,
	},
	Buffer
	{
		binding: u32,
		buffer: vk::Buffer,
		offset: u64,
		range: u64,
		buffer_type: BufferType,
	},
}

impl DescriptorWrite
{
	pub fn descriptor_type(&self) -> vk::DescriptorType
	{
		match self {
			Self::Image { .. } => vk::DescriptorType::COMBINED_IMAGE_SAMPLER,
			Self::Buffer { buffer_type, .. } => buffer_type.descriptor_type(),
		}
	}

	pub fn binding(&self) -> u32
	{
		match self {
			Self::Image { binding, .. } | Self::Buffer { binding, .. } => *binding,
		}
	}
}

impl Device
{
	pub fn create_descriptor_pool(
		&self,
		sizes: &[vk::DescriptorPoolSize],
		max_sets: u32,
		free_sets: bool,
	) -> VkResult<vk::DescriptorPool>
	{
		let mut info = vk::DescriptorPoolCreateInfo::default()
			.max_sets(max_sets)
			.pool_sizes(sizes);

		if free_sets {
			info = info.flags(vk::DescriptorPoolCreateFlags::FREE_DESCRIPTOR_SET);
		}

		// SAFETY: the device is alive.
		unsafe { self.raw().create_descriptor_pool(&info, None) }
	}

	/// # Safety
	///
	/// The pool must belong to this device and none of its sets may be in use.
	pub unsafe fn destroy_descriptor_pool(&self, pool: vk::DescriptorPool)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().destroy_descriptor_pool(pool, None) };
	}

	pub fn allocate_descriptor_set(
		&self,
		pool: vk::DescriptorPool,
		layout: vk::DescriptorSetLayout,
	) -> VkResult<vk::DescriptorSet>
	{
		let layouts = [layout];
		let info = vk::DescriptorSetAllocateInfo::default()
			.descriptor_pool(pool)
			.set_layouts(&layouts);

		// SAFETY: the pool and layout belong to this device.
		unsafe { self.raw().allocate_descriptor_sets(&info) }.map(|sets| sets[0])
	}

	/// # Safety
	///
	/// The pool must have been created with free sets, and the set must come from it and not be in
	/// use.
	pub unsafe fn free_descriptor_set(&self, pool: vk::DescriptorPool, set: vk::DescriptorSet)
	{
		// SAFETY: guaranteed by the caller.
		let _ = unsafe { self.raw().free_descriptor_sets(pool, &[set]) };
	}

	/// # Safety
	///
	/// The set must belong to this device and not be in use by pending work, and every handle in
	/// `writes` must be live.
	pub unsafe fn update_descriptor_set(&self, set: vk::DescriptorSet, writes: &[DescriptorWrite])
	{
		let image_infos: Vec<_> = writes
			.iter()
			.map(|write| match write {
				DescriptorWrite::Image { sampler, view, .. } => {
					[vk::DescriptorImageInfo::default()
						.sampler(*sampler)
						.image_view(*view)
						.image_layout(vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL)]
				}
				DescriptorWrite::Buffer { .. } => [vk::DescriptorImageInfo::default()],
			})
			.collect();

		let buffer_infos: Vec<_> = writes
			.iter()
			.map(|write| match write {
				DescriptorWrite::Buffer {
					buffer,
					offset,
					range,
					..
				} => [vk::DescriptorBufferInfo::default()
					.buffer(*buffer)
					.offset(*offset)
					.range(*range)],
				DescriptorWrite::Image { .. } => [vk::DescriptorBufferInfo::default()],
			})
			.collect();

		let vk_writes: Vec<_> = writes
			.iter()
			.enumerate()
			.map(|(index, write)| {
				let base = vk::WriteDescriptorSet::default()
					.dst_set(set)
					.dst_binding(write.binding())
					.descriptor_type(write.descriptor_type());

				match write {
					DescriptorWrite::Image { .. } => base.image_info(&image_infos[index]),
					DescriptorWrite::Buffer { .. } => base.buffer_info(&buffer_infos[index]),
				}
			})
			.collect();

		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().update_descriptor_sets(&vk_writes, &[]) };
	}

	/// # Safety
	///
	/// `cmd` must be recording, and the layout, sets and offsets must be compatible with the bound
	/// pipeline.
	pub unsafe fn cmd_bind_descriptor_sets(
		&self,
		cmd: vk::CommandBuffer,
		bind_point: vk::PipelineBindPoint,
		layout: vk::PipelineLayout,
		first_set: u32,
		sets: &[vk::DescriptorSet],
		offsets: &[u32],
	)
	{
		// SAFETY: guaranteed by the caller.
		unsafe {
			self.raw()
				.cmd_bind_descriptor_sets(cmd, bind_point, layout, first_set, sets, offsets)
		};
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn write_types_follow_the_resource()
	{
		let image = DescriptorWrite::Image {
			binding: 3,
			sampler: vk::Sampler::null(),
			view: vk::ImageView::null(),
		};
		let uniform = DescriptorWrite::Buffer {
			binding: 4,
			buffer: vk::Buffer::null(),
			offset: 0,
			range: 16,
			buffer_type: BufferType::UniformWithOffset,
		};
		let storage = DescriptorWrite::Buffer {
			binding: 5,
			buffer: vk::Buffer::null(),
			offset: 0,
			range: 16,
			buffer_type: BufferType::Storage,
		};

		assert_eq!(
			image.descriptor_type(),
			vk::DescriptorType::COMBINED_IMAGE_SAMPLER
		);
		assert_eq!(
			uniform.descriptor_type(),
			vk::DescriptorType::UNIFORM_BUFFER_DYNAMIC
		);
		assert_eq!(
			storage.descriptor_type(),
			vk::DescriptorType::STORAGE_BUFFER_DYNAMIC
		);
		assert_eq!(
			(image.binding(), uniform.binding(), storage.binding()),
			(3, 4, 5)
		);
	}
}
