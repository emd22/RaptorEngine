use std::sync::Arc;

use ash::vk;
use raptor_gpu::{Error, ImageFormat, Result, SwapchainRequest};

use crate::core::GpuCore;
use crate::image::Image;

pub struct Swapchain {
	core: Arc<GpuCore>,
	handle: vk::SwapchainKHR,
	extent: (u32, u32),
	format: ImageFormat,
	color_space: vk::ColorSpaceKHR,
	images: Vec<Image>,
}

impl Swapchain {
	pub fn create(core: &Arc<GpuCore>, surface: vk::SurfaceKHR, size: (u32, u32)) -> Result<Self> {
		let mut swapchain = Self {
			core: core.clone(),
			handle: vk::SwapchainKHR::null(),
			extent: size,
			format: ImageFormat::None,
			color_space: vk::ColorSpaceKHR::SRGB_NONLINEAR,
			images: Vec::new(),
		};

		swapchain.build(surface, size)?;

		Ok(swapchain)
	}

	fn build(&mut self, surface: vk::SurfaceKHR, size: (u32, u32)) -> Result<()> {
		let device = self.core.device();

		let info = device.create_swapchain(&SwapchainRequest {
			surface,
			size,
			old: self.handle,
		})?;

		self.handle = info.handle;
		self.extent = size;
		self.format = info.format;
		self.color_space = info.color_space;

		for image in self.images.drain(..) {
			image.destroy_now();
		}

		let raw_images = device
			.swapchain_images(self.handle)
			.map_err(|result| Error::vulkan("Could not get the swapchain images", result))?;

		for raw in raw_images {
			let image = Image::new(&self.core);
			image.wrap_external(raw, size, self.format);
			image.recreate_color_view().map_err(|result| {
				Error::vulkan("Could not create a swapchain image view", result)
			})?;

			self.images.push(image);
		}

		Ok(())
	}

	pub fn rebuild(&mut self, surface: vk::SurfaceKHR, size: (u32, u32)) -> Result<()> {
		self.core.wait_idle();

		raptor_core::log_info!(Render; "Recreating Swapchain");

		self.build(surface, size)
	}

	pub fn handle(&self) -> vk::SwapchainKHR {
		self.handle
	}

	pub fn extent(&self) -> (u32, u32) {
		self.extent
	}

	pub fn aspect_ratio(&self) -> f32 {
		assert!(self.extent.0 > 0 && self.extent.1 > 0);

		self.extent.0 as f32 / self.extent.1 as f32
	}

	pub fn format(&self) -> ImageFormat {
		self.format
	}

	pub fn color_space(&self) -> vk::ColorSpaceKHR {
		self.color_space
	}

	pub fn images(&self) -> &[Image] {
		&self.images
	}

	pub fn image_count(&self) -> usize {
		self.images.len()
	}

	pub fn destroy(&mut self) {
		if self.handle == vk::SwapchainKHR::null() {
			return;
		}

		self.core.wait_idle();

		for image in self.images.drain(..) {
			image.destroy_now();
		}

		// SAFETY: the device is idle, so no image of the swapchain is in use.
		unsafe { self.core.device().destroy_swapchain(self.handle) };

		self.handle = vk::SwapchainKHR::null();
	}
}

impl Drop for Swapchain {
	fn drop(&mut self) {
		self.destroy();
	}
}
