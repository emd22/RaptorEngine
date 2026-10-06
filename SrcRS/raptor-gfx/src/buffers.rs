use std::sync::{Arc, Mutex};

use ash::vk;
use raptor_gpu::{BufferType, ImageFormat, ImageType, Memory};

use crate::buffer::{FLAG_PERSISTENT_MAPPED, GpuBuffer};
use crate::core::GpuCore;
use crate::image::{Image, desc_2d};
use crate::limits::*;
use crate::texture::Textures;
use crate::uniform::Uniforms;
use crate::upload::UploadContext;
use crate::uploader::{UploadSpec, Uploader};

#[derive(Clone, Copy, Debug)]
pub struct RendererBufferSizes {
	pub light_slot: u32,
	pub decal: u32,
	pub probe_sh: u32,
	pub probe_volume: u32,
	pub reflection_probe: u32,
}

pub struct RendererBuffers {
	pub light: Mutex<Uniforms>,
	pub bone: Mutex<Uniforms>,

	pub light_grid: GpuBuffer,
	pub light_index_list: GpuBuffer,
	pub light_grid_page_size: u32,
	pub light_index_list_page_size: u32,

	pub decal: GpuBuffer,
	pub decal_mask: GpuBuffer,
	pub decal_page_size: u32,
	pub decal_mask_page_size: u32,

	pub probe: GpuBuffer,
	pub probe_page_size: u32,
	pub probe_volume: GpuBuffer,
	pub probe_volume_page_size: u32,
	pub probe_grid: GpuBuffer,
	pub probe_grid_page_size: u32,
	pub probe_moments_atlas: Image,

	pub reflection_probe: GpuBuffer,
	pub reflection_probe_page_size: u32,
	pub reflection_probes: Image,
}

impl RendererBuffers {
	pub fn create(
		core: &Arc<GpuCore>,
		textures: &Textures,
		upload: &UploadContext,
		sizes: &RendererBufferSizes,
		supports_cube_arrays: bool,
	) -> Result<Self, vk::Result> {
		let frames = FRAMES_IN_FLIGHT;

		let light = Uniforms::create(
			core,
			frames,
			sizes.light_slot,
			MAX_ACTIVE_LIGHTS,
			BufferType::UniformWithOffset,
		)?;
		let bone = Uniforms::create(
			core,
			frames,
			64,
			MAX_BONE_MATRICES,
			BufferType::StorageWithOffset,
		)?;

		let light_grid_page_size = MAX_SCREEN_TILES * 4 * 4;
		let light_index_list_page_size = MAX_SCREEN_TILES * MAX_LIGHTS_PER_TILE * 4;

		let storage = |size: u64, memory, flags| {
			GpuBuffer::with_data(core, BufferType::StorageWithOffset, size, memory, flags)
		};

		let light_grid = storage(
			u64::from(light_grid_page_size) * u64::from(frames),
			Memory::GpuOnly,
			0,
		)?;
		let light_index_list = storage(
			u64::from(light_index_list_page_size) * u64::from(frames),
			Memory::GpuOnly,
			0,
		)?;

		let decal_page_size = MAX_VISIBLE_DECALS * sizes.decal;
		let decal_mask_page_size = MAX_SCREEN_TILES * DECAL_MASK_WORDS * 4;

		let decal = storage(
			u64::from(decal_page_size) * u64::from(frames),
			Memory::CpuOnly,
			FLAG_PERSISTENT_MAPPED,
		)?;
		let decal_mask = storage(
			u64::from(decal_mask_page_size) * u64::from(frames),
			Memory::GpuOnly,
			0,
		)?;

		let probe_page_size = MAX_IRRADIANCE_PROBES * sizes.probe_sh;
		let probe = storage(
			u64::from(probe_page_size),
			Memory::AutoPreferDevice,
			FLAG_PERSISTENT_MAPPED,
		)?;

		let probe_volume_page_size = MAX_PROBE_VOLUMES * sizes.probe_volume;
		let probe_volume = storage(
			u64::from(probe_volume_page_size),
			Memory::AutoPreferDevice,
			FLAG_PERSISTENT_MAPPED,
		)?;

		let probe_grid_page_size = MAX_PROBE_GRID_POINTS * 2;
		let probe_grid = storage(
			u64::from(probe_grid_page_size),
			Memory::AutoPreferDevice,
			FLAG_PERSISTENT_MAPPED,
		)?;

		let probe_moments_atlas = Self::create_moments_atlas(core, textures, upload)?;

		let reflection_probe_page_size = MAX_REFLECTION_PROBES * sizes.reflection_probe;
		let reflection_probe = storage(
			u64::from(reflection_probe_page_size),
			Memory::AutoPreferDevice,
			FLAG_PERSISTENT_MAPPED,
		)?;

		if !supports_cube_arrays {
			raptor_core::log_error!(Render; "The GPU has no cubemap arrays, which the reflection probes need");
		}

		let reflection_probes = Self::create_reflection_probes(core, textures, upload)?;

		Ok(Self {
			light: Mutex::new(light),
			bone: Mutex::new(bone),
			light_grid,
			light_index_list,
			light_grid_page_size,
			light_index_list_page_size,
			decal,
			decal_mask,
			decal_page_size,
			decal_mask_page_size,
			probe,
			probe_page_size,
			probe_volume,
			probe_volume_page_size,
			probe_grid,
			probe_grid_page_size,
			probe_moments_atlas,
			reflection_probe,
			reflection_probe_page_size,
			reflection_probes,
		})
	}

	fn create_moments_atlas(
		core: &Arc<GpuCore>,
		textures: &Textures,
		upload: &UploadContext,
	) -> Result<Image, vk::Result> {
		let size = (PROBE_ATLAS_WIDTH, PROBE_ATLAS_HEIGHT);
		let bytes = u64::from(size.0)
			* u64::from(size.1)
			* u64::from(ImageFormat::Rg16UNorm.pixel_stride());
		let blank = vec![0xFFu8; bytes as usize];

		let (_, atlas) = textures.new_texture();

		upload.immediate(|cmd| {
			let uploader = Uploader::new(core, cmd, upload.family());

			let spec = UploadSpec {
				image_type: ImageType::Flat,
				size,
				format: ImageFormat::Rg16UNorm,
				mip_level: 0,
				mip_count: 1,
				data: &blank,
			};

			if let Err(error) = uploader.create_from_data(atlas.record(), &spec, false) {
				raptor_core::log_error!(Render; "Could not create the probe moments atlas: {error:?}");
			}
		})?;

		Ok(atlas)
	}

	fn create_reflection_probes(
		core: &Arc<GpuCore>,
		textures: &Textures,
		upload: &UploadContext,
	) -> Result<Image, vk::Result> {
		let (_, image) = textures.new_texture();

		let mut desc = desc_2d(
			(REFLECTION_PROBE_SIZE, REFLECTION_PROBE_SIZE),
			ImageFormat::Rgba16Float,
			vk::ImageUsageFlags::SAMPLED | vk::ImageUsageFlags::TRANSFER_DST,
		);
		desc.image_type = ImageType::CubemapArray;
		desc.mips = REFLECTION_PROBE_MIPS;
		desc.cube_count = MAX_REFLECTION_PROBES;

		image.create(&desc)?;

		upload.immediate(|cmd| {
			crate::barrier::transition_image(
				core.device(),
				cmd,
				upload.family(),
				&image,
				vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
				0,
				REFLECTION_PROBE_MIPS,
			);
		})?;

		Ok(image)
	}

	pub fn light_grid_frame_offset(&self, frame: u32) -> u32 {
		self.light_grid_page_size * frame
	}

	pub fn light_index_list_frame_offset(&self, frame: u32) -> u32 {
		self.light_index_list_page_size * frame
	}

	pub fn decal_frame_offset(&self, frame: u32) -> u32 {
		self.decal_page_size * frame
	}

	pub fn decal_mask_frame_offset(&self, frame: u32) -> u32 {
		self.decal_mask_page_size * frame
	}

	pub fn destroy(&self) {
		self.light
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.destroy();
		self.bone
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.destroy();

		for buffer in [
			&self.light_grid,
			&self.light_index_list,
			&self.decal,
			&self.decal_mask,
			&self.probe,
			&self.probe_volume,
			&self.probe_grid,
			&self.reflection_probe,
		] {
			buffer.destroy();
		}

		self.probe_moments_atlas.release_resources();
		self.reflection_probes.release_resources();
	}
}
