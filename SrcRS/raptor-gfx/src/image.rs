use std::sync::Arc;

use ash::vk::{self, Handle};
use raptor_gpu::{ImageDesc, ImageFormat, ImageRecord, ImageType};

use crate::core::GpuCore;

pub fn desc_2d(size: (u32, u32), format: ImageFormat, usage: vk::ImageUsageFlags) -> ImageDesc {
	ImageDesc {
		image_type: ImageType::Flat,
		size,
		mips: 1,
		format,
		tiling: vk::ImageTiling::OPTIMAL,
		usage,
		aspect: format.aspect_mask(),
		is_target: false,
		cube_count: 1,
		initial_layout: vk::ImageLayout::UNDEFINED,
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ImageInfo {
	pub size: (u32, u32),
	pub image_type: ImageType,
	pub format: ImageFormat,
	pub mip_level: u32,
	pub mip_count: u32,
}

pub struct Image {
	core: Arc<GpuCore>,
	record: Option<Arc<ImageRecord>>,
}

// SAFETY: an image's record is externally synchronised by its users, like the Vulkan handles it
// holds: one thread creates, uploads to and destroys it at a time.
unsafe impl Send for Image {}
// SAFETY: as above, shared references only read the fields.
unsafe impl Sync for Image {}

impl Image {
	pub fn new(core: &Arc<GpuCore>) -> Self {
		Self {
			core: core.clone(),
			record: Some(ImageRecord::new()),
		}
	}

	pub fn create_new(core: &Arc<GpuCore>, desc: &ImageDesc) -> Result<Self, vk::Result> {
		let image = Self::new(core);
		image.create(desc)?;
		Ok(image)
	}

	pub fn record(&self) -> &Arc<ImageRecord> {
		self.record
			.as_ref()
			.expect("the image record is only taken on drop")
	}

	pub fn core(&self) -> &Arc<GpuCore> {
		&self.core
	}

	pub fn create(&self, desc: &ImageDesc) -> Result<(), vk::Result> {
		assert!(desc.size.0 > 0 && desc.size.1 > 0, "an image needs a size");

		self.release_resources();

		// SAFETY: the device and allocator belong together, and the record was emptied above.
		unsafe {
			self.record()
				.create(self.core.device(), self.core.allocator(), desc)
		}
	}

	pub fn wrap_external(&self, image: vk::Image, size: (u32, u32), format: ImageFormat) {
		self.release_resources();

		// SAFETY: the record holds no resources after `release_resources`.
		unsafe { self.record().wrap_external(image, size, format) };
	}

	pub fn recreate_color_view(&self) -> Result<(), vk::Result> {
		// SAFETY: the caller has waited for the GPU to be idle, so the old view is not in use.
		unsafe { self.record().recreate_color_view(self.core.device()) }
	}

	pub fn destroy_now(&self) {
		if let Some(resource) = self.record().detach() {
			// SAFETY: the caller guarantees the GPU is idle, and the core's device and allocator
			// made the image.
			unsafe { resource.destroy(self.core.device(), self.core.allocator()) };
		}
	}

	pub fn release_resources(&self) {
		if let Some(resource) = self.record().detach() {
			self.core.retire_image(resource);
		}
	}

	pub fn info(&self) -> ImageInfo {
		let fields = self.record().fields();

		ImageInfo {
			size: (fields.width, fields.height),
			image_type: ImageType::from_raw(u32::from(fields.image_type))
				.unwrap_or(ImageType::Flat),
			format: ImageFormat::from_raw(fields.format).unwrap_or(ImageFormat::None),
			mip_level: fields.mip_level,
			mip_count: fields.mip_count,
		}
	}

	pub fn set_info(&self, size: (u32, u32), format: ImageFormat, mip_level: u32, mip_count: u32) {
		self.record().set_info(size, format, mip_level, mip_count);
	}

	pub fn size(&self) -> (u32, u32) {
		let fields = self.record().fields();
		(fields.width, fields.height)
	}

	pub fn set_size(&self, size: (u32, u32)) {
		self.record().set_size(size);
	}

	pub fn format(&self) -> ImageFormat {
		ImageFormat::from_raw(self.record().fields().format).unwrap_or(ImageFormat::None)
	}

	pub fn mip_level(&self) -> u32 {
		self.record().fields().mip_level
	}

	pub fn set_mip_level(&self, level: u32) {
		self.record().set_mip_level(level);
	}

	pub fn mip_count(&self) -> u32 {
		self.record().fields().mip_count
	}

	pub fn aspect(&self) -> vk::ImageAspectFlags {
		vk::ImageAspectFlags::from_raw(self.record().fields().aspect)
	}

	pub fn layout(&self) -> vk::ImageLayout {
		vk::ImageLayout::from_raw(self.record().fields().layout)
	}

	pub fn set_layout(&self, layout: vk::ImageLayout) {
		self.record().set_layout(layout);
	}

	pub fn handle(&self) -> vk::Image {
		vk::Image::from_raw(self.record().fields().image)
	}

	pub fn view(&self) -> vk::ImageView {
		vk::ImageView::from_raw(self.record().fields().view)
	}

	pub fn is_created(&self) -> bool {
		self.record().fields().image != 0
	}

	pub fn same_image(&self, other: &Image) -> bool {
		Arc::ptr_eq(self.record(), other.record())
	}
}

impl Clone for Image {
	fn clone(&self) -> Self {
		Self {
			core: self.core.clone(),
			record: self.record.clone(),
		}
	}
}

impl Drop for Image {
	fn drop(&mut self) {
		let Some(record) = self.record.take().and_then(Arc::into_inner) else {
			return;
		};

		if let Some(resource) = record.detach() {
			self.core.retire_image(resource);
		}
	}
}
