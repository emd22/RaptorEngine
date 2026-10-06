use ash::vk;
use raptor_gpu::{ImageFormat, ImageType};

use crate::context::Gfx;
use crate::image::Image;
use crate::uploader::{UploadSpec, Uploader};

pub fn from_pixels(
	gfx: &Gfx,
	size: (u32, u32),
	format: ImageFormat,
	data: &[u8],
) -> Result<Image, vk::Result>
{
	let (_, image) = gfx.textures().new_texture();
	let upload = gfx.upload();

	let mut failure = None;

	upload.immediate(|cmd| {
		let uploader = Uploader::new(gfx.core(), cmd, upload.family());

		let spec = UploadSpec {
			image_type: ImageType::Flat,
			size,
			format,
			mip_level: 0,
			mip_count: 1,
			data,
		};

		failure = uploader.create_from_data(image.record(), &spec, false).err();
	})?;

	match failure {
		Some(error) => Err(error),
		None => Ok(image),
	}
}

pub fn random_noise(gfx: &Gfx, size: (u32, u32), seed: u64) -> Result<Image, vk::Result>
{
	let mut state = seed | 1;
	let count = u64::from(size.0) * u64::from(size.1);

	let mut pixels = Vec::with_capacity((count * 4) as usize);

	for _ in 0..count {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		pixels.extend_from_slice(&(state as u32).to_le_bytes());
	}

	from_pixels(gfx, size, ImageFormat::R32UInt, &pixels)
}

pub fn from_rg16(gfx: &Gfx, size: u32, texels: &[u16]) -> Result<Image, vk::Result>
{
	assert_eq!(texels.len(), size as usize * size as usize * 2);

	from_pixels(gfx, (size, size), ImageFormat::Rg16UNorm, bytemuck::cast_slice(texels))
}

pub fn read_back(gfx: &Gfx, image: &Image) -> Result<Vec<u8>, vk::Result>
{
	use raptor_gpu::{BufferType, Memory};

	let (width, height) = image.size();
	let bytes = u64::from(width) * u64::from(height) * u64::from(image.format().pixel_stride());

	let staging = crate::buffer::GpuBuffer::with_data(
		gfx.core(),
		BufferType::Transfer,
		bytes,
		Memory::GpuToCpu,
		crate::buffer::FLAG_TRANSFER_RECEIVER,
	)?;

	let device = gfx.device();
	let family = gfx.frame_cmd().queue_family();

	gfx.submit_one_time_cmd(|cmd| {
		crate::barrier::transition_image(
			device,
			cmd,
			family,
			image,
			vk::ImageLayout::TRANSFER_SRC_OPTIMAL,
			0,
			1,
		);

		let copy = vk::BufferImageCopy::default()
			.image_subresource(
				vk::ImageSubresourceLayers::default()
					.aspect_mask(vk::ImageAspectFlags::COLOR)
					.layer_count(1),
			)
			.image_extent(vk::Extent3D {
				width,
				height,
				depth: 1,
			});

		// SAFETY: the command buffer is recording, the image is in the transfer source layout
		// and the staging buffer holds the whole image.
		unsafe {
			device.raw().cmd_copy_image_to_buffer(
				cmd,
				image.handle(),
				vk::ImageLayout::TRANSFER_SRC_OPTIMAL,
				staging.handle(),
				&[copy],
			)
		};

		crate::barrier::transition_image(
			device,
			cmd,
			family,
			image,
			vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
			0,
			1,
		);
	})?;

	staging.map()?;
	staging.invalidate()?;

	staging.with_mapped(|data| data.to_vec())
}
