use std::ffi::c_void;

use ash::vk::{self, Handle};
use raptor_gpu::{
	FinalViews, ImageFormat, ImageRecord, ImageType, RenderStageRecord, TargetConfig,
};

use crate::gpu::RxGpuDevice;
use crate::gpu_resources::RxGpuAllocator;

pub type RxRenderStage = RenderStageRecord;

pub const NO_TARGET: u32 = u32::MAX;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxTargetConfig
{
	pub format: u16,
	pub image_type: u16,
	pub usage: u32,
	pub aspect: u32,
	pub samples: i32,
	pub load_op: i32,
	pub store_op: i32,
	pub stencil_load_op: i32,
	pub stencil_store_op: i32,
	pub initial_layout: i32,
	pub final_layout: i32,
	pub width: u32,
	pub height: u32,
	pub render_pass_only: u8,
}

fn rect(x: i32, y: i32, width: u32, height: u32) -> vk::Rect2D
{
	vk::Rect2D {
		offset: vk::Offset2D { x, y },
		extent: vk::Extent2D { width, height },
	}
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_render_stage_new() -> *mut RxRenderStage
{
	Box::into_raw(RenderStageRecord::new())
}

/// Destroys the stage, and the images of its targets.
///
/// # Safety
///
/// `stage` must be null or come from `rx_render_stage_new`, with the GPU done with everything it
/// made, and must not be used afterwards. `device` and `allocator` may both be null to leak what it
/// made.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_render_stage_destroy(
	stage: *mut RxRenderStage,
	device: *const RxGpuDevice,
	allocator: *const RxGpuAllocator,
)
{
	if stage.is_null() {
		return;
	}

	// SAFETY: guaranteed by the caller.
	let stage = unsafe { Box::from_raw(stage) };

	// SAFETY: guaranteed by the caller.
	if let (Some(device), Some(allocator)) = unsafe { (device.as_ref(), allocator.as_ref()) } {
		// SAFETY: guaranteed by the caller.
		unsafe { stage.destroy(&device.device, &allocator.0) };
	}
}

/// Adds a target of the given format and size, and returns its index. If `reference` is not null
/// the target uses that image rather than making its own.
///
/// # Safety
///
/// `stage` and `config` must be live, and `reference` null or a live image.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_render_stage_add_target(
	stage: *mut RxRenderStage,
	config: *const RxTargetConfig,
	reference: *const ImageRecord,
) -> u32
{
	// SAFETY: guaranteed by the caller.
	let config = unsafe { &*config };

	let target = TargetConfig {
		image_type: ImageType::from_raw(u32::from(config.image_type)).unwrap_or(ImageType::Flat),
		usage: vk::ImageUsageFlags::from_raw(config.usage),
		aspect: vk::ImageAspectFlags::from_raw(config.aspect),
		samples: vk::SampleCountFlags::from_raw(config.samples as u32),
		load_op: vk::AttachmentLoadOp::from_raw(config.load_op),
		store_op: vk::AttachmentStoreOp::from_raw(config.store_op),
		stencil_load_op: vk::AttachmentLoadOp::from_raw(config.stencil_load_op),
		stencil_store_op: vk::AttachmentStoreOp::from_raw(config.stencil_store_op),
		initial_layout: vk::ImageLayout::from_raw(config.initial_layout),
		final_layout: vk::ImageLayout::from_raw(config.final_layout),
		render_pass_only: config.render_pass_only != 0,
	};

	let reference = if reference.is_null() {
		None
	} else {
		// SAFETY: guaranteed by the caller.
		Some(unsafe { ImageRecord::arc_from_raw(reference) })
	};

	// SAFETY: guaranteed by the caller.
	unsafe { &mut *stage }.add_target(
		target,
		ImageFormat::from_raw(config.format).unwrap_or(ImageFormat::None),
		(config.width, config.height),
		reference,
	) as u32
}

/// # Safety
///
/// `stage` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_render_stage_target_count(stage: *const RxRenderStage) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*stage }.targets().len() as u32
}

/// The image of a target, which lives as long as the stage does, or null if there is no such
/// target.
///
/// # Safety
///
/// `stage` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_render_stage_target_image(
	stage: *const RxRenderStage,
	index: u32,
) -> *const ImageRecord
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*stage }
		.targets()
		.get(index as usize)
		.map_or(std::ptr::null(), |target| {
			std::sync::Arc::as_ptr(&target.image)
		})
}

/// Where the `sub_index`th target of `format` is, or `u32::MAX`.
///
/// # Safety
///
/// `stage` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_render_stage_find_target(
	stage: *const RxRenderStage,
	format: u16,
	sub_index: i32,
) -> u32
{
	// SAFETY: guaranteed by the caller.
	let index = unsafe { &*stage }.targets().find(format, sub_index);

	u32::try_from(index).unwrap_or(NO_TARGET)
}

/// Writes the formats of the targets that are not depth, up to `capacity`, and returns how many
/// there are.
///
/// # Safety
///
/// `stage` must be live and `out` valid for `capacity` formats, or null with a capacity of 0.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_render_stage_color_target_formats(
	stage: *const RxRenderStage,
	out: *mut u16,
	capacity: usize,
) -> usize
{
	// SAFETY: guaranteed by the caller.
	let formats = unsafe { &*stage }.targets().color_formats();
	let written = formats.len().min(capacity);

	if written > 0 {
		// SAFETY: guaranteed by the caller.
		unsafe { std::ptr::copy_nonoverlapping(formats.as_ptr(), out, written) };
	}

	formats.len()
}

/// Gets the render pass attachment of each target, which stays valid until a target is added or the
/// stage built again.
///
/// # Safety
///
/// `stage` must be live and the outputs writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_render_stage_descriptions(
	stage: *mut RxRenderStage,
	out_descriptions: *mut *const c_void,
	out_count: *mut usize,
)
{
	// SAFETY: guaranteed by the caller.
	let descriptions = unsafe { &mut *stage }.descriptions();

	// SAFETY: guaranteed by the caller.
	unsafe {
		*out_descriptions = descriptions.as_ptr().cast();
		*out_count = descriptions.len();
	}
}

/// Makes the stage's images, render pass and framebuffers at `width` by `height`, or makes them
/// again if `recreate`. The final stage passes the views of the swapchain images, which it draws to
/// instead.
///
/// # Safety
///
/// `stage`, `device` and `allocator` must be live, nothing the stage made before may be in use, and
/// `final_views` valid for `final_view_count` live views.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_render_stage_build(
	stage: *mut RxRenderStage,
	device: *const RxGpuDevice,
	allocator: *const RxGpuAllocator,
	width: u32,
	height: u32,
	final_views: *const u64,
	final_view_count: usize,
	final_width: u32,
	final_height: u32,
	recreate: u8,
) -> i32
{
	let views: Vec<vk::ImageView> = if final_view_count == 0 {
		Vec::new()
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(final_views, final_view_count) }
			.iter()
			.map(|view| vk::ImageView::from_raw(*view))
			.collect()
	};

	let final_views = (final_view_count > 0).then_some(FinalViews {
		views: &views,
		size: (final_width, final_height),
	});

	// SAFETY: guaranteed by the caller.
	let result = unsafe {
		(*stage).build(
			&(*device).device,
			&(*allocator).0,
			(width, height),
			final_views,
			recreate != 0,
		)
	};

	match result {
		Ok(()) => vk::Result::SUCCESS.as_raw(),
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `stage` and `device` must be live, `cmd` recording outside a render pass, and the stage built.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_render_stage_begin(
	stage: *const RxRenderStage,
	device: *const RxGpuDevice,
	cmd: *mut c_void,
	image_index: u32,
	render_x: i32,
	render_y: i32,
	render_width: u32,
	render_height: u32,
	draw_x: i32,
	draw_y: i32,
	draw_width: u32,
	draw_height: u32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*stage).begin(
			&(*device).device,
			vk::CommandBuffer::from_raw(cmd as u64),
			image_index,
			rect(render_x, render_y, render_width, render_height),
			rect(draw_x, draw_y, draw_width, draw_height),
		)
	};
}

/// # Safety
///
/// `stage` and `device` must be live and `cmd` inside the stage's render pass.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_render_stage_end(
	stage: *const RxRenderStage,
	device: *const RxGpuDevice,
	cmd: *mut c_void,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { (*stage).end(&(*device).device, vk::CommandBuffer::from_raw(cmd as u64)) };
}
