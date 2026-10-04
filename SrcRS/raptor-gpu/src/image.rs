use ash::prelude::VkResult;
use ash::vk;

use crate::format::{ImageFormat, ImageType, mip_dimensions};
use crate::{AllocRequest, Allocation, Allocator, Device, Memory};

#[derive(Clone, Copy, Debug)]
pub struct ImageDesc
{
	pub image_type: ImageType,
	pub size: (u32, u32),
	pub mips: u32,
	pub format: ImageFormat,
	pub tiling: vk::ImageTiling,
	pub usage: vk::ImageUsageFlags,
	pub aspect: vk::ImageAspectFlags,
	pub is_target: bool,
	pub cube_count: u32,
	pub initial_layout: vk::ImageLayout,
}

pub struct Image
{
	pub image: vk::Image,
	pub view: vk::ImageView,
	pub allocation: Allocation,
}

#[derive(Debug)]
pub enum CopyError
{
	Misaligned,
}

impl Image
{
	/// # Safety
	///
	/// The device and allocator must be live, and the allocator must belong to the device.
	pub unsafe fn create(device: &Device, allocator: &Allocator, desc: &ImageDesc)
	-> VkResult<Self>
	{
		let (view_type, layer_count) = desc.image_type.properties(desc.cube_count);

		let families = device.families();
		let queue_families = [families.graphics, families.transfer];

		let concurrent = !desc.is_target && families.has_independent_transfer();

		let mut info = vk::ImageCreateInfo::default()
			.image_type(vk::ImageType::TYPE_2D)
			.format(desc.format.to_vk())
			.extent(vk::Extent3D {
				width: desc.size.0,
				height: desc.size.1,
				depth: 1,
			})
			.mip_levels(desc.mips)
			.array_layers(layer_count)
			.samples(vk::SampleCountFlags::TYPE_1)
			.tiling(desc.tiling)
			.usage(desc.usage)
			.sharing_mode(vk::SharingMode::EXCLUSIVE)
			.initial_layout(desc.initial_layout);

		if desc.image_type.is_cube() {
			info = info.flags(vk::ImageCreateFlags::CUBE_COMPATIBLE);
		}

		if concurrent {
			info = info
				.sharing_mode(vk::SharingMode::CONCURRENT)
				.queue_family_indices(&queue_families);
		}

		let request = AllocRequest {
			memory: Memory::Auto,
			dedicated: true,
			priority: 1.0,
			..Default::default()
		};

		// SAFETY: the create info and the queue family array outlive the call.
		let (image, allocation) = unsafe { allocator.create_image(&info, &request) }?;

		let view_info = vk::ImageViewCreateInfo::default()
			.image(image)
			.view_type(view_type)
			.format(desc.format.to_vk())
			.components(vk::ComponentMapping::default())
			.subresource_range(
				vk::ImageSubresourceRange::default()
					.aspect_mask(desc.aspect)
					.level_count(desc.mips)
					.layer_count(layer_count),
			);

		// SAFETY: the image was just created on this device.
		match unsafe { device.raw().create_image_view(&view_info, None) } {
			Ok(view) => Ok(Self {
				image,
				view,
				allocation,
			}),
			Err(error) => {
				// SAFETY: the image and allocation were just created and nothing else uses them.
				unsafe { allocator.destroy_image(image, allocation) };
				Err(error)
			}
		}
	}

	/// # Safety
	///
	/// The image must not be in use by pending work, and must not be used afterwards.
	pub unsafe fn destroy(self, device: &Device, allocator: &Allocator)
	{
		if self.view != vk::ImageView::null() {
			// SAFETY: guaranteed by the caller.
			unsafe { device.raw().destroy_image_view(self.view, None) };
		}

		// SAFETY: guaranteed by the caller.
		unsafe { allocator.destroy_image(self.image, self.allocation) };
	}
}

#[derive(Clone, Copy, Debug)]
pub struct MipChain
{
	pub format: ImageFormat,
	pub size: (u32, u32),
	pub mips: u32,
	pub aspect: vk::ImageAspectFlags,
}

impl MipChain
{
	pub fn regions(&self) -> Result<Vec<vk::BufferImageCopy>, CopyError>
	{
		let stride = u64::from(self.format.pixel_stride());
		let mut regions = Vec::with_capacity(self.mips as usize);
		let mut offset = 0u64;

		for mip in 0..self.mips {
			if !offset.is_multiple_of(4) {
				return Err(CopyError::Misaligned);
			}

			let (width, height) = mip_dimensions(self.size, mip);

			regions.push(
				vk::BufferImageCopy::default()
					.buffer_offset(offset)
					.image_subresource(
						vk::ImageSubresourceLayers::default()
							.aspect_mask(self.aspect)
							.mip_level(mip)
							.layer_count(1),
					)
					.image_extent(vk::Extent3D {
						width,
						height,
						depth: 1,
					}),
			);

			offset += u64::from(width) * u64::from(height) * stride;
		}

		Ok(regions)
	}
}

impl Device
{
	/// # Safety
	///
	/// `cmd` must be recording, the image must be in `TRANSFER_DST_OPTIMAL`, and the buffer must
	/// hold the whole chain.
	pub unsafe fn cmd_copy_buffer_to_mips(
		&self,
		cmd: vk::CommandBuffer,
		buffer: vk::Buffer,
		image: vk::Image,
		chain: &MipChain,
	) -> Result<(), CopyError>
	{
		let regions = chain.regions()?;

		// SAFETY: guaranteed by the caller.
		unsafe {
			self.raw().cmd_copy_buffer_to_image(
				cmd,
				buffer,
				image,
				vk::ImageLayout::TRANSFER_DST_OPTIMAL,
				&regions,
			)
		};

		Ok(())
	}

	/// # Safety
	///
	/// `cmd` must be recording, and the image must be in `TRANSFER_DST_OPTIMAL`.
	pub unsafe fn cmd_copy_buffer_to_region(
		&self,
		cmd: vk::CommandBuffer,
		buffer: vk::Buffer,
		image: vk::Image,
		mip: u32,
		size: (u32, u32),
		offset: (i32, i32),
	)
	{
		let region = vk::BufferImageCopy::default()
			.image_subresource(
				vk::ImageSubresourceLayers::default()
					.aspect_mask(vk::ImageAspectFlags::COLOR)
					.mip_level(mip)
					.layer_count(1),
			)
			.image_offset(vk::Offset3D {
				x: offset.0,
				y: offset.1,
				z: 0,
			})
			.image_extent(vk::Extent3D {
				width: size.0,
				height: size.1,
				depth: 1,
			});

		// SAFETY: guaranteed by the caller.
		unsafe {
			self.raw().cmd_copy_buffer_to_image(
				cmd,
				buffer,
				image,
				vk::ImageLayout::TRANSFER_DST_OPTIMAL,
				&[region],
			)
		};
	}
}

impl Device
{
	pub fn create_color_view(
		&self,
		image: vk::Image,
		format: ImageFormat,
	) -> VkResult<vk::ImageView>
	{
		let info = vk::ImageViewCreateInfo::default()
			.image(image)
			.view_type(vk::ImageViewType::TYPE_2D)
			.format(format.to_vk())
			.components(vk::ComponentMapping::default())
			.subresource_range(
				vk::ImageSubresourceRange::default()
					.aspect_mask(vk::ImageAspectFlags::COLOR)
					.level_count(1)
					.layer_count(1),
			);

		// SAFETY: the image belongs to this device.
		unsafe { self.raw().create_image_view(&info, None) }
	}

	/// # Safety
	///
	/// The view must belong to this device and not be in use.
	pub unsafe fn destroy_view(&self, view: vk::ImageView)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().destroy_image_view(view, None) };
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn mip_chain_offsets_accumulate()
	{
		let regions = MipChain {
			format: ImageFormat::Rgba8UNorm,
			size: (8, 4),
			mips: 3,
			aspect: vk::ImageAspectFlags::COLOR,
		}
		.regions()
		.unwrap();

		assert_eq!(regions.len(), 3);
		assert_eq!(regions[0].buffer_offset, 0);
		assert_eq!(regions[1].buffer_offset, 8 * 4 * 4);
		assert_eq!(regions[2].buffer_offset, 8 * 4 * 4 + 4 * 2 * 4);
		assert_eq!(
			(
				regions[2].image_extent.width,
				regions[2].image_extent.height
			),
			(2, 1)
		);
		assert_eq!(regions[2].image_subresource.mip_level, 2);
	}

	#[test]
	fn unaligned_mips_are_rejected()
	{
		let result = MipChain {
			format: ImageFormat::R8UNorm,
			size: (3, 3),
			mips: 2,
			aspect: vk::ImageAspectFlags::COLOR,
		}
		.regions();

		assert!(matches!(result, Err(CopyError::Misaligned)));
	}
}
