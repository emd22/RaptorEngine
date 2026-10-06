use std::sync::{Arc, Mutex, MutexGuard};

use ash::vk::{self, Handle};
use raptor_gfx::limits::*;
use raptor_gfx::{Gfx, GpuBuffer, Uploader, gfx};
use raptor_gpu::{BufferType, ImageFormat, Memory};

use crate::gpu_ops::{transition_raw, transition_record, write_buffer_range};
use crate::probe_data::REFLECTION_HALFS_PER_PROBE;
use crate::probes::{CAPTURE_SIZE, CaptureKind, PROBES_PER_FRAME, ProbeGpu};
use crate::stage::{RenderStage, StageTarget};

const FACES: usize = 6;

pub struct CaptureResources {
	pub capture_stage: RenderStage,
	pub reflection_stage: RenderStage,
	color_staging: Vec<GpuBuffer>,
	depth_staging: Vec<GpuBuffer>,
	reflection_staging: Vec<GpuBuffer>,
}

#[derive(Clone, Default)]
pub struct CaptureHandle(Arc<Mutex<Option<CaptureResources>>>);

impl CaptureHandle {
	pub fn resources(&self) -> MutexGuard<'_, Option<CaptureResources>> {
		self.0
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
	}

	pub fn with_stage<R>(&self, kind: CaptureKind, f: impl FnOnce(&RenderStage) -> R) -> Option<R> {
		let resources = self.resources();

		resources.as_ref().map(|resources| match kind {
			CaptureKind::Irradiance => f(&resources.capture_stage),
			CaptureKind::Reflection => f(&resources.reflection_stage),
		})
	}
}

pub struct GfxProbeGpu {
	handle: CaptureHandle,
}

impl GfxProbeGpu {
	pub fn new() -> (Self, CaptureHandle) {
		let handle = CaptureHandle::default();

		(
			Self {
				handle: handle.clone(),
			},
			handle,
		)
	}
}

fn capture_stage(gfx: &Gfx, name: &'static str, size: u32, depth_transfer: bool) -> RenderStage {
	let stage = RenderStage::new(gfx, name, Some((size, size)), 1);

	let transfer_src = vk::ImageUsageFlags::TRANSFER_SRC;

	stage.add_target(StageTarget::color(ImageFormat::Rgba16Float).with_usage(
		vk::ImageUsageFlags::COLOR_ATTACHMENT | vk::ImageUsageFlags::SAMPLED | transfer_src,
	));

	let depth_usage = vk::ImageUsageFlags::DEPTH_STENCIL_ATTACHMENT
		| vk::ImageUsageFlags::SAMPLED
		| if depth_transfer {
			transfer_src
		} else {
			vk::ImageUsageFlags::empty()
		};

	stage.add_target(StageTarget::depth(ImageFormat::D32Float).with_usage(depth_usage));

	stage.build(gfx);

	stage
}

fn staging(gfx: &Gfx, bytes: u64) -> GpuBuffer {
	GpuBuffer::with_data(
		gfx.core(),
		BufferType::Transfer,
		bytes,
		Memory::GpuToCpu,
		raptor_gfx::buffer::FLAG_TRANSFER_RECEIVER,
	)
	.expect("a probe capture staging buffer can be made")
}

impl CaptureResources {
	fn new(gfx: &Gfx) -> Self {
		let pixels = u64::from(CAPTURE_SIZE) * u64::from(CAPTURE_SIZE);

		let slots = PROBES_PER_FRAME as usize * FACES;

		let color_staging = (0..slots).map(|_| staging(gfx, pixels * 8)).collect();
		let depth_staging = (0..slots).map(|_| staging(gfx, pixels * 4)).collect();

		let reflection_pixels = u64::from(REFLECTION_PROBE_SIZE) * u64::from(REFLECTION_PROBE_SIZE);

		let reflection_staging = (0..FACES)
			.map(|_| staging(gfx, reflection_pixels * 8))
			.collect();

		Self {
			capture_stage: capture_stage(gfx, "ProbeCapture", CAPTURE_SIZE, true),
			reflection_stage: capture_stage(gfx, "ReflectionCapture", REFLECTION_PROBE_SIZE, false),
			color_staging,
			depth_staging,
			reflection_staging,
		}
	}
}

fn copy_target_to_staging(
	gfx: &Gfx,
	stage: &RenderStage,
	format: ImageFormat,
	size: u32,
	staging: &GpuBuffer,
) {
	let cmd = gfx.frame_cmd();

	let Some(image) = stage.target_image(format) else {
		return;
	};

	let aspect = if format == ImageFormat::D32Float {
		vk::ImageAspectFlags::DEPTH
	} else {
		vk::ImageAspectFlags::COLOR
	};

	transition_record(
		gfx,
		cmd,
		&image,
		vk::ImageLayout::TRANSFER_SRC_OPTIMAL,
		0,
		1,
	);

	let copy = vk::BufferImageCopy::default()
		.image_subresource(
			vk::ImageSubresourceLayers::default()
				.aspect_mask(aspect)
				.layer_count(1),
		)
		.image_extent(vk::Extent3D {
			width: size,
			height: size,
			depth: 1,
		});

	// SAFETY: the command buffer is recording, the image is in the transfer source layout and the
	// staging buffer is large enough for the copy.
	unsafe {
		gfx.device().raw().cmd_copy_image_to_buffer(
			cmd.raw(),
			vk::Image::from_raw(image.fields().image),
			vk::ImageLayout::TRANSFER_SRC_OPTIMAL,
			staging.handle(),
			&[copy],
		)
	};

	transition_record(
		gfx,
		cmd,
		&image,
		vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
		0,
		1,
	);
}

fn read_staging<T: Copy>(buffer: &GpuBuffer, count: usize) -> Option<Vec<T>> {
	buffer.map().ok()?;
	let _ = buffer.invalidate();

	let pointer = buffer.mapped_ptr();

	let result =
		(!pointer.is_null() && buffer.size() as usize >= count * size_of::<T>()).then(|| {
			// SAFETY: the buffer is mapped and holds at least `count` values, and the types read are
			// plain numbers.
			unsafe { std::slice::from_raw_parts(pointer.cast::<T>(), count) }.to_vec()
		});

	buffer.unmap();

	result
}

impl ProbeGpu for GfxProbeGpu {
	fn wait_idle(&mut self) {
		gfx().wait_idle();
	}

	fn write_volumes(&mut self, bytes: &[u8]) {
		write_buffer_range(&gfx().buffers().probe_volume, 0, bytes);
	}

	fn write_grid(&mut self, bytes: &[u8]) {
		write_buffer_range(&gfx().buffers().probe_grid, 0, bytes);
	}

	fn write_probes(&mut self, offset: u64, bytes: &[u8]) {
		write_buffer_range(&gfx().buffers().probe, offset, bytes);
	}

	fn write_moments_row(&mut self, atlas_row: u32, texels: &[u16]) {
		let gfx = gfx();
		let family = gfx.upload().family();
		let atlas = gfx.buffers().probe_moments_atlas.record().clone();

		let uploader = Uploader::new(gfx.core(), vk::CommandBuffer::null(), family);

		let Ok(staging) = uploader.staging(bytemuck::cast_slice(texels)) else {
			return;
		};

		let result = gfx.submit_immediate_upload(|cmd| {
			transition_raw(
				&gfx,
				cmd,
				family,
				&atlas,
				vk::ImageLayout::TRANSFER_DST_OPTIMAL,
				0,
				1,
			);

			// SAFETY: the command buffer is recording and the image is in the transfer layout.
			unsafe {
				gfx.device().cmd_copy_buffer_to_region(
					cmd,
					vk::Buffer::from_raw(staging.fields().buffer),
					vk::Image::from_raw(atlas.fields().image),
					0,
					(PROBE_ATLAS_WIDTH, PROBE_DEPTH_SIZE),
					(0, (atlas_row * PROBE_DEPTH_SIZE) as i32),
				)
			};

			transition_raw(
				&gfx,
				cmd,
				family,
				&atlas,
				vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
				0,
				1,
			);
		});

		if let Err(error) = result {
			raptor_core::log_error!(Render; "Could not upload the probe moments: {error:?}");
		}

		uploader.retire_staging(staging);
	}

	fn write_reflection_probes(&mut self, bytes: &[u8]) {
		write_buffer_range(&gfx().buffers().reflection_probe, 0, bytes);
	}

	fn write_reflection_cubemap(&mut self, probe: u32, halfs: &[u16]) {
		let gfx = gfx();
		let family = gfx.upload().family();
		let image = gfx.buffers().reflection_probes.record().clone();

		let uploader = Uploader::new(gfx.core(), vk::CommandBuffer::null(), family);

		debug_assert_eq!(halfs.len() as u64, REFLECTION_HALFS_PER_PROBE);

		let Ok(staging) = uploader.staging(bytemuck::cast_slice(halfs)) else {
			return;
		};

		let mut regions = Vec::with_capacity(REFLECTION_PROBE_MIPS as usize);
		let mut offset = 0u64;

		for mip in 0..REFLECTION_PROBE_MIPS {
			let size = REFLECTION_PROBE_SIZE >> mip;

			regions.push(
				vk::BufferImageCopy::default()
					.buffer_offset(offset)
					.image_subresource(
						vk::ImageSubresourceLayers::default()
							.aspect_mask(vk::ImageAspectFlags::COLOR)
							.mip_level(mip)
							.base_array_layer(probe * REFLECTION_PROBE_FACES)
							.layer_count(REFLECTION_PROBE_FACES),
					)
					.image_extent(vk::Extent3D {
						width: size,
						height: size,
						depth: 1,
					}),
			);

			offset += u64::from(size) * u64::from(size) * u64::from(REFLECTION_PROBE_FACES) * 8;
		}

		let result = gfx.submit_immediate_upload(|cmd| {
			transition_raw(
				&gfx,
				cmd,
				family,
				&image,
				vk::ImageLayout::TRANSFER_DST_OPTIMAL,
				0,
				REFLECTION_PROBE_MIPS,
			);

			// SAFETY: the command buffer is recording, the image is in the transfer layout and the
			// staging buffer holds every region.
			unsafe {
				gfx.device().raw().cmd_copy_buffer_to_image(
					cmd,
					vk::Buffer::from_raw(staging.fields().buffer),
					vk::Image::from_raw(image.fields().image),
					vk::ImageLayout::TRANSFER_DST_OPTIMAL,
					&regions,
				)
			};

			transition_raw(
				&gfx,
				cmd,
				family,
				&image,
				vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
				0,
				REFLECTION_PROBE_MIPS,
			);
		});

		if let Err(error) = result {
			raptor_core::log_error!(Render; "Could not upload a reflection probe: {error:?}");
		}

		uploader.retire_staging(staging);
	}

	fn create_capture_resources(&mut self) {
		let mut resources = self.handle.resources();

		if resources.is_none() {
			*resources = Some(CaptureResources::new(&gfx()));
		}
	}

	fn copy_capture_to_staging(&mut self, kind: CaptureKind, slot: u32, face: u32) {
		let gfx = gfx();
		let resources = self.handle.resources();

		let Some(resources) = resources.as_ref() else {
			return;
		};

		let index = slot as usize * FACES + face as usize;

		match kind {
			CaptureKind::Irradiance => {
				copy_target_to_staging(
					&gfx,
					&resources.capture_stage,
					ImageFormat::Rgba16Float,
					CAPTURE_SIZE,
					&resources.color_staging[index],
				);
				copy_target_to_staging(
					&gfx,
					&resources.capture_stage,
					ImageFormat::D32Float,
					CAPTURE_SIZE,
					&resources.depth_staging[index],
				);
			}
			CaptureKind::Reflection => copy_target_to_staging(
				&gfx,
				&resources.reflection_stage,
				ImageFormat::Rgba16Float,
				REFLECTION_PROBE_SIZE,
				&resources.reflection_staging[face as usize],
			),
		}
	}

	fn read_capture_color(&mut self, kind: CaptureKind, slot: u32, face: u32) -> Option<Vec<u16>> {
		let resources = self.handle.resources();
		let resources = resources.as_ref()?;

		match kind {
			CaptureKind::Irradiance => read_staging(
				&resources.color_staging[slot as usize * FACES + face as usize],
				(CAPTURE_SIZE * CAPTURE_SIZE * 4) as usize,
			),
			CaptureKind::Reflection => read_staging(
				&resources.reflection_staging[face as usize],
				(REFLECTION_PROBE_SIZE * REFLECTION_PROBE_SIZE * 4) as usize,
			),
		}
	}

	fn read_capture_depth(&mut self, slot: u32, face: u32) -> Option<Vec<f32>> {
		let resources = self.handle.resources();
		let resources = resources.as_ref()?;

		read_staging(
			&resources.depth_staging[slot as usize * FACES + face as usize],
			(CAPTURE_SIZE * CAPTURE_SIZE) as usize,
		)
	}
}
