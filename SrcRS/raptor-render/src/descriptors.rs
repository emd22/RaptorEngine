use std::sync::Arc;

use ash::vk::{self, Handle};
use raptor_gfx::{CommandBuffer, Gfx};
use raptor_gpu::{
	BufferRecord, DescriptorEntryRef, DescriptorResourceRef, DescriptorSetRecord, ImageRecord,
};

use crate::pipeline_cache::PipelineSlot;

pub enum Entry<'a> {
	Buffer {
		binding: u32,
		stages: vk::ShaderStageFlags,
		buffer: &'a Arc<BufferRecord>,
		offset: u64,
		range: u64,
	},
	Image {
		binding: u32,
		stages: vk::ShaderStageFlags,
		image: &'a Arc<ImageRecord>,
		sampler: vk::Sampler,
	},
}

impl Entry<'_> {
	fn as_ref(&self) -> DescriptorEntryRef {
		match self {
			Entry::Buffer {
				binding,
				stages,
				buffer,
				offset,
				range,
			} => DescriptorEntryRef {
				binding: *binding,
				stages: *stages,
				resource: DescriptorResourceRef::Buffer {
					buffer: Arc::as_ptr(buffer),
					offset: *offset,
					range: *range,
				},
			},
			Entry::Image {
				binding,
				stages,
				image,
				sampler,
			} => DescriptorEntryRef {
				binding: *binding,
				stages: *stages,
				resource: DescriptorResourceRef::Image {
					image: Arc::as_ptr(image),
					sampler: *sampler,
				},
			},
		}
	}
}

#[derive(Clone, Copy)]
pub struct DescriptorSet(*mut DescriptorSetRecord);

// SAFETY: the record lives in the descriptor cache until it is freed and is only read to bind.
unsafe impl Send for DescriptorSet {}
// SAFETY: as above.
unsafe impl Sync for DescriptorSet {}

impl DescriptorSet {
	pub fn request(gfx: &Gfx, entries: &[Entry]) -> Self {
		let refs: Vec<_> = entries.iter().map(Entry::as_ref).collect();

		// SAFETY: every record is held by the caller's `Arc` for the call, and the layout cache is
		// the backend's own.
		let result = unsafe {
			gfx.descriptors()
				.request(gfx.device(), gfx.ds_layouts(), &refs)
		};

		match result {
			Ok((_, set)) => Self(set),
			Err(error) => {
				raptor_core::log_fatal!(Render; "Could not make a descriptor set: {error:?}");
				panic!("Could not make a descriptor set: {error:?}");
			}
		}
	}

	pub fn bind(
		&self,
		gfx: &Gfx,
		cmd: &CommandBuffer,
		slot: &PipelineSlot,
		first_set: u32,
		offsets: &[u32],
	) {
		let bind_point = if slot.is_compute != 0 {
			vk::PipelineBindPoint::COMPUTE
		} else {
			vk::PipelineBindPoint::GRAPHICS
		};

		// SAFETY: the record is live in the cache, the command buffer is recording, and the layout
		// is the bound pipeline's.
		unsafe {
			(*self.0).bind(
				gfx.device(),
				cmd.raw(),
				bind_point,
				vk::PipelineLayout::from_raw(slot.layout),
				first_set,
				offsets,
			)
		};
	}
}
