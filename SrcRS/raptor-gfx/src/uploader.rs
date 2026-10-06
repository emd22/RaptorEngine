use std::sync::Arc;

use ash::vk::{self, Handle};
use raptor_gpu::{
	BufferRecord, BufferResource, BufferType, CopyError, ImageDesc, ImageFormat, ImageRecord,
	ImageType, LayoutTransition, Memory, MipChain, mip_dimensions,
};

use crate::buffer::FLAG_TRANSFER_RECEIVER;
use crate::core::GpuCore;

pub struct UploadSpec<'a> {
	pub image_type: ImageType,
	pub size: (u32, u32),
	pub format: ImageFormat,
	pub mip_level: u32,
	pub mip_count: u32,
	pub data: &'a [u8],
}

#[derive(Debug)]
pub enum UploadError {
	Vulkan(vk::Result),
	Misaligned,
}

impl From<vk::Result> for UploadError {
	fn from(error: vk::Result) -> Self {
		Self::Vulkan(error)
	}
}

pub struct Uploader<'a> {
	pub core: &'a GpuCore,
	pub cmd: vk::CommandBuffer,
	pub queue_family: u32,
	pub retire: Option<&'a dyn Fn(BufferResource)>,
}

impl<'a> Uploader<'a> {
	pub fn new(core: &'a GpuCore, cmd: vk::CommandBuffer, queue_family: u32) -> Self {
		Self {
			core,
			cmd,
			queue_family,
			retire: None,
		}
	}

	pub fn staging(&self, data: &[u8]) -> Result<Arc<BufferRecord>, vk::Result> {
		let record = BufferRecord::new();
		let allocator = self.core.allocator();

		// SAFETY: the allocator is live, and the record is empty.
		unsafe {
			record.create(
				allocator,
				BufferType::Transfer,
				data.len() as u64,
				Memory::CpuToGpu,
				FLAG_TRANSFER_RECEIVER,
			)?;
			record.upload(allocator, data)?;
		}

		Ok(record)
	}

	pub fn retire_staging(&self, staging: Arc<BufferRecord>) {
		if let Some(resource) = staging.detach() {
			match self.retire {
				Some(retire) => retire(resource),
				None => self.core.retire_buffer(resource),
			}
		}
	}

	fn transition(
		&self,
		image: &ImageRecord,
		new_layout: vk::ImageLayout,
		base_mip: u32,
		levels: u32,
	) {
		let fields = image.fields();

		// SAFETY: the command buffer is recording and the image is live, as the caller
		// guarantees.
		unsafe {
			self.core.device().cmd_image_layout_transition(
				self.cmd,
				&LayoutTransition {
					image: vk::Image::from_raw(fields.image),
					aspect: vk::ImageAspectFlags::from_raw(fields.aspect),
					old_layout: vk::ImageLayout::from_raw(fields.layout),
					new_layout,
					base_mip,
					levels,
					cmd_queue_family: self.queue_family,
				},
			)
		};

		image.set_layout(new_layout);
	}

	fn create_image(
		&self,
		image: &ImageRecord,
		spec: &UploadSpec,
		is_target: bool,
	) -> Result<(), vk::Result> {
		let format = spec.format;

		let desc = ImageDesc {
			image_type: spec.image_type,
			size: spec.size,
			mips: spec.mip_count,
			format,
			tiling: vk::ImageTiling::OPTIMAL,
			usage: vk::ImageUsageFlags::TRANSFER_DST
				| vk::ImageUsageFlags::TRANSFER_SRC
				| format.usage_flags()
				| vk::ImageUsageFlags::SAMPLED,
			aspect: format.aspect_mask(),
			is_target,
			cube_count: 1,
			initial_layout: vk::ImageLayout::UNDEFINED,
		};

		// SAFETY: the image's resources are not in use, as the caller guarantees.
		unsafe { image.create(self.core.device(), self.core.allocator(), &desc) }
	}

	pub fn create_from_data(
		&self,
		image: &ImageRecord,
		spec: &UploadSpec,
		is_target: bool,
	) -> Result<(), vk::Result> {
		let staging = self.staging(spec.data)?;

		self.create_image(image, spec, is_target)?;

		let (width, height) = mip_dimensions(spec.size, spec.mip_level);

		self.transition(
			image,
			vk::ImageLayout::TRANSFER_DST_OPTIMAL,
			spec.mip_level,
			1,
		);

		// SAFETY: the image is in the transfer layout and the command buffer is recording.
		unsafe {
			self.core.device().cmd_copy_buffer_to_region(
				self.cmd,
				vk::Buffer::from_raw(staging.fields().buffer),
				vk::Image::from_raw(image.fields().image),
				spec.mip_level,
				(width, height),
				(0, 0),
			)
		};

		self.transition(
			image,
			vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
			spec.mip_level,
			1,
		);

		image.set_mip_level(spec.mip_level);

		self.retire_staging(staging);

		Ok(())
	}

	pub fn upload_chain(&self, image: &ImageRecord, spec: &UploadSpec) -> Result<(), UploadError> {
		let staging = self.staging(spec.data)?;
		self.create_image(image, spec, false)?;

		self.transition(
			image,
			vk::ImageLayout::TRANSFER_DST_OPTIMAL,
			0,
			spec.mip_count,
		);

		// SAFETY: the image is in the transfer layout and the command buffer is recording.
		let copied = unsafe {
			self.core.device().cmd_copy_buffer_to_mips(
				self.cmd,
				vk::Buffer::from_raw(staging.fields().buffer),
				vk::Image::from_raw(image.fields().image),
				&MipChain {
					format: spec.format,
					size: spec.size,
					mips: spec.mip_count,
					aspect: spec.format.aspect_mask(),
				},
			)
		};

		self.transition(
			image,
			vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
			0,
			spec.mip_count,
		);

		image.set_mip_level(spec.mip_level);

		self.retire_staging(staging);

		copied.map_err(|CopyError::Misaligned| UploadError::Misaligned)
	}

	pub fn copy_buffer_to_mip(
		&self,
		image: &ImageRecord,
		buffer: vk::Buffer,
		size: (u32, u32),
		mip_level: u32,
		offset: (i32, i32),
	) {
		self.transition(image, vk::ImageLayout::TRANSFER_DST_OPTIMAL, mip_level, 1);

		// SAFETY: the image is in the transfer layout and the command buffer is recording.
		unsafe {
			self.core.device().cmd_copy_buffer_to_region(
				self.cmd,
				buffer,
				vk::Image::from_raw(image.fields().image),
				mip_level,
				size,
				offset,
			)
		};

		self.transition(
			image,
			vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
			mip_level,
			1,
		);
	}
}
