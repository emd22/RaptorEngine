use std::cell::UnsafeCell;
use std::sync::Arc;

use ash::prelude::VkResult;
use ash::vk;

use crate::{Allocation, Allocator, Device, Image, ImageDesc, ImageFormat};

pub const DEFAULT_FORMAT: u16 = ImageFormat::Rgba8UNorm as u16;

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ImageFields
{
	pub image: u64,
	pub view: u64,
	pub layout: i32,
	pub aspect: u32,
	pub width: u32,
	pub height: u32,
	pub mip_count: u32,
	pub mip_level: u32,
	pub format: u16,
	pub image_type: u16,
}

impl Default for ImageFields
{
	fn default() -> Self
	{
		Self {
			image: 0,
			view: 0,
			layout: vk::ImageLayout::UNDEFINED.as_raw(),
			aspect: vk::ImageAspectFlags::COLOR.as_raw(),
			width: 0,
			height: 0,
			mip_count: 1,
			mip_level: 0,
			format: DEFAULT_FORMAT,
			image_type: 0,
		}
	}
}

struct ImageState
{
	allocation: Option<Allocation>,
	owns_image: bool,
}

/// Shared between every C++ copy of an image through `Arc::into_raw`. The fields are read and
/// written from C++ through the pointer, so access has to be externally synchronised, like the
/// Vulkan handles they hold.
#[repr(C)]
pub struct ImageRecord
{
	fields: UnsafeCell<ImageFields>,
	state: UnsafeCell<ImageState>,
}

impl ImageRecord
{
	#[allow(clippy::arc_with_non_send_sync)]
	pub fn new() -> Arc<Self>
	{
		Arc::new(Self {
			fields: UnsafeCell::new(ImageFields::default()),
			state: UnsafeCell::new(ImageState {
				allocation: None,
				owns_image: false,
			}),
		})
	}

	pub fn into_raw(this: Arc<Self>) -> *mut Self
	{
		Arc::into_raw(this).cast_mut()
	}

	pub fn fields(&self) -> ImageFields
	{
		// SAFETY: access is externally synchronised, and no reference into the cell outlives a
		// call.
		unsafe { *self.fields.get() }
	}

	pub fn has_resources(&self) -> bool
	{
		let fields = self.fields();

		fields.image != 0 || fields.view != 0
	}

	pub fn owns_image(&self) -> bool
	{
		// SAFETY: access is externally synchronised, and no reference into the cell outlives a
		// call.
		unsafe { (*self.state.get()).owns_image }
	}

	/// # Safety
	///
	/// `record` must come from `into_raw`.
	pub unsafe fn retain(record: *const Self)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { Arc::increment_strong_count(record) };
	}

	/// # Safety
	///
	/// `record` must come from `into_raw` and not be used by the caller afterwards. The device and
	/// allocator must be the ones the image was created with, or `None` to leak the resources, and
	/// the image must not be in use when the last reference goes.
	pub unsafe fn release(
		record: *const Self,
		device: Option<&Device>,
		allocator: Option<&Allocator>,
	) -> bool
	{
		// SAFETY: guaranteed by the caller.
		let Some(record) = Arc::into_inner(unsafe { Arc::from_raw(record) }) else {
			return false;
		};

		if let (Some(device), Some(allocator)) = (device, allocator) {
			// SAFETY: guaranteed by the caller, and the last reference is gone.
			unsafe { record.destroy_resources(device, allocator) };
		}

		true
	}

	/// # Safety
	///
	/// The device and allocator must be the ones the image was created with, the image must not be
	/// in use, and nothing else may access the record during the call.
	pub unsafe fn destroy_resources(&self, device: &Device, allocator: &Allocator)
	{
		use ash::vk::Handle;

		// SAFETY: guaranteed by the caller.
		let (fields, state) = unsafe { (&mut *self.fields.get(), &mut *self.state.get()) };

		if fields.view != 0 {
			// SAFETY: guaranteed by the caller.
			unsafe { device.destroy_view(vk::ImageView::from_raw(fields.view)) };
		}

		if state.owns_image
			&& fields.image != 0
			&& let Some(allocation) = state.allocation.take()
		{
			// SAFETY: guaranteed by the caller.
			unsafe { allocator.destroy_image(vk::Image::from_raw(fields.image), allocation) };
		}

		fields.image = 0;
		fields.view = 0;
		state.allocation = None;
		state.owns_image = false;
	}

	/// # Safety
	///
	/// As for `destroy_resources`, and the allocator must belong to the device.
	pub unsafe fn create(
		&self,
		device: &Device,
		allocator: &Allocator,
		desc: &ImageDesc,
	) -> VkResult<()>
	{
		use ash::vk::Handle;

		// SAFETY: guaranteed by the caller.
		unsafe { self.destroy_resources(device, allocator) };

		// SAFETY: guaranteed by the caller.
		let created = unsafe { Image::create(device, allocator, desc) }?;

		// SAFETY: guaranteed by the caller.
		let (fields, state) = unsafe { (&mut *self.fields.get(), &mut *self.state.get()) };

		*fields = ImageFields {
			image: created.image.as_raw(),
			view: created.view.as_raw(),
			layout: vk::ImageLayout::UNDEFINED.as_raw(),
			aspect: desc.aspect.as_raw(),
			width: desc.size.0,
			height: desc.size.1,
			mip_count: desc.mips,
			mip_level: 0,
			format: desc.format as u16,
			image_type: desc.image_type as u16,
		};

		state.allocation = Some(created.allocation);
		state.owns_image = true;

		Ok(())
	}

	/// # Safety
	///
	/// Nothing else may access the record during the call, and any resources it holds must already
	/// be released.
	pub unsafe fn wrap_external(&self, image: vk::Image, size: (u32, u32), format: ImageFormat)
	{
		use ash::vk::Handle;

		// SAFETY: guaranteed by the caller.
		let (fields, state) = unsafe { (&mut *self.fields.get(), &mut *self.state.get()) };

		*fields = ImageFields {
			image: image.as_raw(),
			width: size.0,
			height: size.1,
			format: format as u16,
			..ImageFields::default()
		};

		state.allocation = None;
		state.owns_image = false;
	}

	/// # Safety
	///
	/// The device must be the one the image belongs to, the old view must not be in use, and
	/// nothing else may access the record during the call.
	pub unsafe fn recreate_color_view(&self, device: &Device) -> VkResult<()>
	{
		use ash::vk::Handle;

		// SAFETY: guaranteed by the caller.
		let fields = unsafe { &mut *self.fields.get() };

		if fields.view != 0 {
			// SAFETY: guaranteed by the caller.
			unsafe { device.destroy_view(vk::ImageView::from_raw(fields.view)) };
			fields.view = 0;
		}

		let format = ImageFormat::from_raw(fields.format).unwrap_or(ImageFormat::None);
		let view = device.create_color_view(vk::Image::from_raw(fields.image), format)?;

		fields.view = view.as_raw();

		Ok(())
	}
}

#[cfg(test)]
mod tests
{
	use ash::vk::Handle;

	use super::*;

	#[test]
	fn a_new_record_matches_the_old_image_defaults()
	{
		let record = ImageRecord::new();
		let fields = record.fields();

		assert_eq!(fields.image, 0);
		assert_eq!(fields.view, 0);
		assert_eq!(fields.layout, vk::ImageLayout::UNDEFINED.as_raw());
		assert_eq!(fields.aspect, vk::ImageAspectFlags::COLOR.as_raw());
		assert_eq!((fields.width, fields.height), (0, 0));
		assert_eq!((fields.mip_count, fields.mip_level), (1, 0));
		assert_eq!(fields.format, ImageFormat::Rgba8UNorm as u16);
		assert_eq!(Arc::strong_count(&record), 1);
		assert!(!record.has_resources());
	}

	#[test]
	fn the_last_release_frees_the_record_and_earlier_ones_do_not()
	{
		let record = ImageRecord::into_raw(ImageRecord::new());

		// SAFETY: the record was just made and is only used through these calls.
		unsafe {
			ImageRecord::retain(record);
			ImageRecord::retain(record);

			assert!(!ImageRecord::release(record, None, None));
			assert!(!ImageRecord::release(record, None, None));
			assert!(ImageRecord::release(record, None, None));
		}
	}

	#[test]
	fn wrapping_an_external_image_does_not_own_it()
	{
		let record = ImageRecord::new();

		// SAFETY: nothing else uses the record.
		unsafe {
			record.wrap_external(
				vk::Image::from_raw(0x1234),
				(1280, 720),
				ImageFormat::Bgra8Srgb,
			)
		};

		let fields = record.fields();

		assert_eq!(fields.image, 0x1234);
		assert_eq!((fields.width, fields.height), (1280, 720));
		assert_eq!(fields.format, ImageFormat::Bgra8Srgb as u16);
		assert_eq!(fields.mip_count, 1);
		assert!(!record.owns_image());
		assert!(record.has_resources());
	}

	#[test]
	fn the_public_fields_have_the_layout_the_c_header_declares()
	{
		use std::mem::{offset_of, size_of};

		assert_eq!(offset_of!(ImageRecord, fields), 0);
		assert_eq!(size_of::<ImageFields>(), 48);
		assert_eq!(offset_of!(ImageFields, image), 0);
		assert_eq!(offset_of!(ImageFields, view), 8);
		assert_eq!(offset_of!(ImageFields, layout), 16);
		assert_eq!(offset_of!(ImageFields, aspect), 20);
		assert_eq!(offset_of!(ImageFields, width), 24);
		assert_eq!(offset_of!(ImageFields, height), 28);
		assert_eq!(offset_of!(ImageFields, mip_count), 32);
		assert_eq!(offset_of!(ImageFields, mip_level), 36);
		assert_eq!(offset_of!(ImageFields, format), 40);
		assert_eq!(offset_of!(ImageFields, image_type), 42);
	}
}
