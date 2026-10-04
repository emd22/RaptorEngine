use ash::prelude::VkResult;
use ash::vk;

use crate::{Device, Error, ImageFormat, Level, Result};

#[derive(Clone, Copy, Debug)]
pub struct SwapchainRequest
{
	pub surface: vk::SurfaceKHR,
	pub size: (u32, u32),
	pub old: vk::SwapchainKHR,
}

#[derive(Clone, Copy, Debug)]
pub struct SwapchainInfo
{
	pub handle: vk::SwapchainKHR,
	pub format: ImageFormat,
	pub color_space: vk::ColorSpaceKHR,
}

pub fn select_image_count(min: u32, max: u32) -> u32
{
	let wanted = min + 1;

	if max > 0 && wanted > max { max } else { wanted }
}

pub fn surface_image_format(format: vk::Format) -> Option<ImageFormat>
{
	match format {
		vk::Format::B8G8R8A8_SRGB => Some(ImageFormat::Bgra8Srgb),
		vk::Format::R8G8B8A8_SRGB => Some(ImageFormat::Rgba8Srgb),
		vk::Format::R16G16B16A16_SFLOAT => Some(ImageFormat::Rgba16Float),
		vk::Format::R8G8B8A8_UNORM => Some(ImageFormat::Rgba8UNorm),
		_ => None,
	}
}

impl Device
{
	pub fn create_swapchain(&self, request: &SwapchainRequest) -> Result<SwapchainInfo>
	{
		// SAFETY: the physical device and surface are valid.
		let capabilities = unsafe {
			self.surface_loader()
				.get_physical_device_surface_capabilities(self.physical(), request.surface)
		}
		.map_err(|result| Error::vulkan("Error retrieving surface capabilities", result))?;

		let image_count =
			select_image_count(capabilities.min_image_count, capabilities.max_image_count);

		self.log().log(
			Level::Info,
			&format!(
				"Swapchain - Min:{}, Max:{}, Selected:{}",
				capabilities.min_image_count, capabilities.max_image_count, image_count
			),
		);

		let surface_format = self.surface_format()?;

		let Some(format) = surface_image_format(surface_format.format) else {
			return Err(Error::new(format!(
				"Unsupported surface format {:?}",
				surface_format.format
			)));
		};

		let info = vk::SwapchainCreateInfoKHR::default()
			.surface(request.surface)
			.min_image_count(image_count)
			.image_format(format.to_vk())
			.image_color_space(surface_format.color_space)
			.image_extent(vk::Extent2D {
				width: request.size.0,
				height: request.size.1,
			})
			.image_array_layers(1)
			.image_usage(vk::ImageUsageFlags::COLOR_ATTACHMENT)
			.image_sharing_mode(vk::SharingMode::EXCLUSIVE)
			.pre_transform(capabilities.current_transform)
			.composite_alpha(vk::CompositeAlphaFlagsKHR::OPAQUE)
			.present_mode(vk::PresentModeKHR::FIFO)
			.clipped(true)
			.old_swapchain(request.old);

		// SAFETY: the create info outlives the call, and the old swapchain is null or belongs to
		// this device.
		let handle = unsafe { self.swapchain_loader().create_swapchain(&info, None) }
			.map_err(|result| Error::vulkan("Could not create swapchain", result))?;

		if request.old != vk::SwapchainKHR::null() && handle != request.old {
			// SAFETY: the old swapchain was retired by the create call and is no longer presenting.
			unsafe { self.swapchain_loader().destroy_swapchain(request.old, None) };
		}

		Ok(SwapchainInfo {
			handle,
			format,
			color_space: surface_format.color_space,
		})
	}

	pub fn swapchain_images(&self, swapchain: vk::SwapchainKHR) -> VkResult<Vec<vk::Image>>
	{
		// SAFETY: the swapchain belongs to this device.
		unsafe { self.swapchain_loader().get_swapchain_images(swapchain) }
	}

	/// # Safety
	///
	/// The swapchain must belong to this device, and none of its images may be in use.
	pub unsafe fn destroy_swapchain(&self, swapchain: vk::SwapchainKHR)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.swapchain_loader().destroy_swapchain(swapchain, None) };
	}

	pub fn acquire_next_image(
		&self,
		swapchain: vk::SwapchainKHR,
		timeout: u64,
		semaphore: vk::Semaphore,
	) -> (vk::Result, u32)
	{
		let mut index = 0;

		// SAFETY: the swapchain and semaphore belong to this device, and `index` is writable.
		let result = unsafe {
			(self.swapchain_loader().fp().acquire_next_image_khr)(
				self.handle(),
				swapchain,
				timeout,
				semaphore,
				vk::Fence::null(),
				&mut index,
			)
		};

		(result, index)
	}

	/// # Safety
	///
	/// `queue` must be a queue of this device that supports presentation, and must not be in use on
	/// another thread.
	pub unsafe fn queue_present(
		&self,
		queue: vk::Queue,
		swapchain: vk::SwapchainKHR,
		wait_semaphore: vk::Semaphore,
		image_index: u32,
	) -> vk::Result
	{
		let swapchains = [swapchain];
		let wait_semaphores = [wait_semaphore];
		let indices = [image_index];

		let info = vk::PresentInfoKHR::default()
			.wait_semaphores(&wait_semaphores)
			.swapchains(&swapchains)
			.image_indices(&indices);

		// SAFETY: guaranteed by the caller.
		unsafe { (self.swapchain_loader().fp().queue_present_khr)(queue, &info) }
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn image_count_is_one_over_the_minimum_capped_to_the_maximum()
	{
		assert_eq!(select_image_count(2, 8), 3);
		assert_eq!(select_image_count(3, 3), 3);
		assert_eq!(select_image_count(2, 0), 3);
		assert_eq!(select_image_count(4, 4), 4);
	}

	#[test]
	fn only_the_supported_surface_formats_map()
	{
		assert_eq!(
			surface_image_format(vk::Format::B8G8R8A8_SRGB),
			Some(ImageFormat::Bgra8Srgb)
		);
		assert_eq!(
			surface_image_format(vk::Format::R8G8B8A8_UNORM),
			Some(ImageFormat::Rgba8UNorm)
		);
		assert_eq!(
			surface_image_format(vk::Format::A2B10G10R10_UNORM_PACK32),
			None
		);
	}
}
