use std::ffi::{CStr, c_char, c_void};

use ash::vk::{self, Handle};
use raptor_gpu::INVALID_INDEX;
use raptor_render::pipeline_cache::{
	BuildFailure, Builder, GpuBuilder, PipelineCache, PipelineSlot, TemplateFn, TemplateFree,
};
use raptor_render::pipeline_desc::{DeclareFn, Declarer, FreeFn, PipelineDesc};
use raptor_render::shader_library::ShaderLibrary;

use crate::RxLogSink;
use crate::gpu::RxGpuDevice;
use crate::gpu_descriptors::{RxDescriptorCache, RxDescriptorEntry, entry_ref};
use crate::gpu_passes_layouts::RxDsLayoutCache;
use crate::gpu_render_stage::RxRenderStage;
use crate::gpu_tables::{RxBlendAttachment, blend_attachment};
use crate::shader_library::SinkLog;

pub type RxPipelineCache = PipelineCache;
pub type RxPipelineDesc = PipelineDesc;
pub type RxPipelineSlot = PipelineSlot;

pub const PIPELINE_OK: i32 = 0;
pub const PIPELINE_DESCRIPTOR_MISMATCH: i32 = 1;
pub const PIPELINE_VULKAN_ERROR: i32 = 2;
pub const PIPELINE_SHADER_COMPILE_FAILED: i32 = 3;
pub const PIPELINE_SHADER_COMPILER_UNAVAILABLE: i32 = 4;
pub const PIPELINE_BLEND_TARGET: i32 = 5;

#[repr(C)]
pub struct RxPipelineContext
{
	pub device: *const RxGpuDevice,
	pub descriptor_cache: *mut RxDescriptorCache,
	pub ds_layouts: *const RxDsLayoutCache,
	pub library: *mut ShaderLibrary,
	pub log: *const RxLogSink,
}

fn status_of(result: Result<(), BuildFailure>, out_vk_result: *mut i32) -> i32
{
	match result {
		Ok(()) => PIPELINE_OK,
		Err(BuildFailure::DescriptorMismatch) => PIPELINE_DESCRIPTOR_MISMATCH,
		Err(BuildFailure::Vulkan(result)) => {
			if !out_vk_result.is_null() {
				// SAFETY: guaranteed by the caller.
				unsafe { *out_vk_result = result };
			}

			PIPELINE_VULKAN_ERROR
		}
		Err(BuildFailure::ShaderCompile) => PIPELINE_SHADER_COMPILE_FAILED,
		Err(BuildFailure::ShaderCompilerUnavailable) => PIPELINE_SHADER_COMPILER_UNAVAILABLE,
		Err(BuildFailure::BlendTarget) => PIPELINE_BLEND_TARGET,
	}
}

/// # Safety
///
/// The context must be live and everything in it must belong together.
unsafe fn with_builder<T>(
	context: *const RxPipelineContext,
	run: impl FnOnce(&mut dyn Builder) -> T,
) -> T
{
	// SAFETY: guaranteed by the caller.
	let context = unsafe { &*context };

	// SAFETY: guaranteed by the caller.
	let (device, descriptors, ds_layouts, library, log) = unsafe {
		(
			&(*context.device).device,
			&mut *context.descriptor_cache,
			&(*context.ds_layouts).0,
			&mut *context.library,
			&*context.log,
		)
	};

	let mut sink = SinkLog(log);

	let mut builder = GpuBuilder {
		device,
		descriptors,
		ds_layouts,
		library,
		log: &mut sink,
	};

	run(&mut builder)
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_pipeline_cache_new(num_static: u32, max_dynamic: u32) -> *mut RxPipelineCache
{
	Box::into_raw(Box::new(PipelineCache::new(num_static, max_dynamic)))
}

/// Destroys the cache and the pipelines in it. `device` may be null to leak them.
///
/// # Safety
///
/// `cache` must be null or come from `rx_pipeline_cache_new` and must not be used afterwards, and
/// `device` must be the one the pipelines were made on.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_cache_free(
	cache: *mut RxPipelineCache,
	device: *const RxGpuDevice,
)
{
	if cache.is_null() {
		return;
	}

	// SAFETY: guaranteed by the caller.
	let cache = unsafe { *Box::from_raw(cache) };

	// SAFETY: guaranteed by the caller.
	cache.destroy(unsafe { device.as_ref() }.map(|device| &device.device));
}

/// # Safety
///
/// `cache` must be live and `name` NUL terminated.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_cache_set_static_name(
	cache: *const RxPipelineCache,
	handle: u32,
	name: *const c_char,
)
{
	// SAFETY: guaranteed by the caller.
	let (cache, name) = unsafe { (&*cache, CStr::from_ptr(name).to_string_lossy()) };

	cache.set_static_name(handle, &name);
}

/// Gets the pipeline of a handle, or null if there is none.
///
/// # Safety
///
/// `cache` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_cache_slot(
	cache: *const RxPipelineCache,
	handle: u32,
) -> *mut RxPipelineSlot
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*cache }.slot(handle)
}

/// # Safety
///
/// `cache` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_cache_has_pending(cache: *const RxPipelineCache) -> bool
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*cache }.has_pending()
}

/// # Safety
///
/// `cache` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_cache_is_main_thread(cache: *const RxPipelineCache) -> bool
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*cache }.is_main_thread()
}

/// Sets what makes the description of a pipeline that draws `pass`. `call` fills in the
/// description for some features and returns false if there is no pipeline for them, and it is
/// called with the lock of the cache held. `free` is called with `user` when the template is
/// replaced or the cache destroyed.
///
/// # Safety
///
/// `cache` must be live and `call` and `free` valid for `user`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_cache_register_template(
	cache: *const RxPipelineCache,
	pass: u32,
	call: TemplateFn,
	user: *mut c_void,
	free: Option<TemplateFree>,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*cache }.register_template(pass, call, user, free);
}

/// Makes a pipeline from a description and takes the description. On the main thread it is built
/// before this returns, and `out_handle` is invalid if that failed. Elsewhere it is built later.
///
/// # Safety
///
/// `cache`, `context` and `desc` must be live, `desc` must come from `rx_pipeline_desc_new` and
/// not be used afterwards, and the outputs must be writable (`out_vk_result` may be null).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_cache_create(
	cache: *const RxPipelineCache,
	context: *const RxPipelineContext,
	desc: *mut RxPipelineDesc,
	out_handle: *mut u32,
	out_vk_result: *mut i32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let (cache, desc) = unsafe { (&*cache, *Box::from_raw(desc)) };

	// SAFETY: guaranteed by the caller.
	let result = unsafe { with_builder(context, |builder| cache.create(desc, builder)) };

	match result {
		Ok(handle) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out_handle = handle.unwrap_or(INVALID_INDEX) };
			PIPELINE_OK
		}
		Err(error) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out_handle = INVALID_INDEX };
			status_of(Err(error), out_vk_result)
		}
	}
}

/// Builds the pipelines that were asked for on other threads. Call only on the main thread.
///
/// # Safety
///
/// `cache` and `context` must be live and `out_vk_result` writable or null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_cache_build_pending(
	cache: *const RxPipelineCache,
	context: *const RxPipelineContext,
	out_vk_result: *mut i32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let cache = unsafe { &*cache };

	// SAFETY: guaranteed by the caller.
	let result = unsafe { with_builder(context, |builder| cache.build_pending(builder)) };

	status_of(result, out_vk_result)
}

/// Builds one of the pipelines the cache has a slot for from the start and takes the description.
///
/// # Safety
///
/// As for `rx_pipeline_cache_create`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_cache_build_static(
	cache: *const RxPipelineCache,
	context: *const RxPipelineContext,
	handle: u32,
	desc: *mut RxPipelineDesc,
	out_vk_result: *mut i32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let (cache, desc) = unsafe { (&*cache, *Box::from_raw(desc)) };

	// SAFETY: guaranteed by the caller.
	let result =
		unsafe { with_builder(context, |builder| cache.build_static(handle, desc, builder)) };

	status_of(result, out_vk_result)
}

/// # Safety
///
/// `cache` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_cache_find_variant(
	cache: *const RxPipelineCache,
	pass: u32,
	features: u32,
) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*cache }.find_variant(pass, features)
}

/// # Safety
///
/// `cache` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_cache_find_variant_in_pass(
	cache: *const RxPipelineCache,
	handle: u32,
	pass: u32,
) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*cache }.find_variant_in_pass(handle, pass)
}

/// Gets the pipeline that draws `features` in `pass`, making it from the pass's template if it
/// does not exist. `out_handle` is invalid if there is no such pipeline.
///
/// # Safety
///
/// As for `rx_pipeline_cache_create`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_cache_get_or_create_variant(
	cache: *const RxPipelineCache,
	context: *const RxPipelineContext,
	pass: u32,
	features: u32,
	out_handle: *mut u32,
	out_vk_result: *mut i32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let cache = unsafe { &*cache };

	// SAFETY: guaranteed by the caller.
	let result = unsafe {
		with_builder(context, |builder| {
			cache.get_or_create_variant(pass, features, builder)
		})
	};

	finish_handle(result, out_handle, out_vk_result)
}

/// As `rx_pipeline_cache_get_or_create_variant`, for the features `handle` draws.
///
/// # Safety
///
/// As for `rx_pipeline_cache_create`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_cache_get_or_create_variant_in_pass(
	cache: *const RxPipelineCache,
	context: *const RxPipelineContext,
	handle: u32,
	pass: u32,
	out_handle: *mut u32,
	out_vk_result: *mut i32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let cache = unsafe { &*cache };

	// SAFETY: guaranteed by the caller.
	let result = unsafe {
		with_builder(context, |builder| {
			cache.get_or_create_variant_in_pass(handle, pass, builder)
		})
	};

	finish_handle(result, out_handle, out_vk_result)
}

fn finish_handle(
	result: Result<u32, BuildFailure>,
	out_handle: *mut u32,
	out_vk_result: *mut i32,
) -> i32
{
	match result {
		Ok(handle) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out_handle = handle };
			PIPELINE_OK
		}
		Err(error) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out_handle = INVALID_INDEX };
			status_of(Err(error), out_vk_result)
		}
	}
}

/// Gets the `index`th pipeline that draws `pass`, if there is one.
///
/// # Safety
///
/// `cache` must be live and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_cache_pass_pipeline(
	cache: *const RxPipelineCache,
	pass: u32,
	index: usize,
	out: *mut u32,
) -> bool
{
	// SAFETY: guaranteed by the caller.
	match unsafe { &*cache }.registry().pass_pipeline(pass, index) {
		Some(handle) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out = handle };
			true
		}
		None => false,
	}
}

/// # Safety
///
/// `cache` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_cache_add_buffer_offset(
	cache: *const RxPipelineCache,
	set: u32,
	offset: u32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*cache }.add_buffer_offset(set as usize, offset);
}

/// Binds the descriptor sets of a pipeline with the offsets that were added, and forgets the
/// offsets.
///
/// # Safety
///
/// `cache`, `device` and `slot` must be live and `cmd` recording.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_cache_bind_sets(
	cache: *const RxPipelineCache,
	device: *const RxGpuDevice,
	slot: *const RxPipelineSlot,
	cmd: *mut c_void,
	bind_point: i32,
) -> bool
{
	// SAFETY: guaranteed by the caller.
	let (cache, device, slot) = unsafe { (&*cache, &(*device).device, &*slot) };

	let offsets = cache.take_offsets();

	let mut matched = true;

	for index in 0..slot.set_count as usize {
		// SAFETY: the slot holds `set_count` sets.
		let set = unsafe { *slot.sets.add(index) };

		let set_offsets = offsets
			.get(set.index as usize)
			.map_or(&[][..], Vec::as_slice);

		// SAFETY: the set belongs to the descriptor cache, which outlives the pipeline.
		let record = unsafe { &*set.set };

		matched &= set_offsets.len() == record.fields.buffer_count as usize;

		// SAFETY: guaranteed by the caller.
		unsafe {
			record.bind(
				device,
				vk::CommandBuffer::from_raw(cmd as u64),
				vk::PipelineBindPoint::from_raw(bind_point),
				vk::PipelineLayout::from_raw(slot.layout),
				set.index,
				set_offsets,
			)
		};
	}

	matched
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_pipeline_desc_new() -> *mut RxPipelineDesc
{
	Box::into_raw(Box::default())
}

/// # Safety
///
/// `desc` must be null or come from `rx_pipeline_desc_new` and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_desc_free(desc: *mut RxPipelineDesc)
{
	if !desc.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(desc) });
	}
}

/// # Safety
///
/// `desc` must be live and `name` NUL terminated.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_desc_set_debug_name(
	desc: *mut RxPipelineDesc,
	name: *const c_char,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *desc }.debug_name = unsafe { CStr::from_ptr(name) }
		.to_string_lossy()
		.into_owned();
}

/// Sets the shader by its index and its name, and the macros it is compiled with.
///
/// # Safety
///
/// `desc` must be live, `name` NUL terminated, and `macros` valid for `macro_count` entries whose
/// strings are null or NUL terminated.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_desc_set_shader(
	desc: *mut RxPipelineDesc,
	index: u32,
	name: *const c_char,
	macros: *const crate::gpu_pipelines::RxShaderMacroRef,
	macro_count: usize,
)
{
	// SAFETY: guaranteed by the caller.
	let (desc, name) = unsafe { (&mut *desc, CStr::from_ptr(name).to_string_lossy()) };

	desc.set_shader(index, &name);
	desc.macros.clear();

	for index in 0..macro_count {
		// SAFETY: guaranteed by the caller.
		unsafe { add_macro_ref(desc, &*macros.add(index)) };
	}
}

unsafe fn add_macro_ref(desc: &mut PipelineDesc, entry: &crate::gpu_pipelines::RxShaderMacroRef)
{
	let bytes = |pointer: *const c_char| {
		// SAFETY: guaranteed by the caller.
		(!pointer.is_null()).then(|| unsafe { CStr::from_ptr(pointer) }.to_bytes())
	};

	desc.add_macro(bytes(entry.name), bytes(entry.value));
}

/// # Safety
///
/// `desc` must be live and the macro strings null or NUL terminated.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_desc_add_macro(
	desc: *mut RxPipelineDesc,
	name: *const c_char,
	value: *const c_char,
)
{
	let entry = crate::gpu_pipelines::RxShaderMacroRef { name, value };

	// SAFETY: guaranteed by the caller.
	unsafe { add_macro_ref(&mut *desc, &entry) };
}

/// # Safety
///
/// `desc` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_desc_set_vertex_type(
	desc: *mut RxPipelineDesc,
	vertex_type: u32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *desc }.vertex_type = vertex_type;
}

/// # Safety
///
/// `desc` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_desc_set_no_vertices(desc: *mut RxPipelineDesc, value: bool)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *desc }.no_vertices = value;
}

/// # Safety
///
/// `desc` must be live and `stage` live until the pipeline is built, or null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_desc_set_stage(
	desc: *mut RxPipelineDesc,
	stage: *mut RxRenderStage,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *desc }.stage = stage;
}

/// # Safety
///
/// `desc` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_desc_set_cull_mode(desc: *mut RxPipelineDesc, cull_mode: u32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *desc }.cull_mode = cull_mode;
}

/// # Safety
///
/// `desc` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_desc_set_front_face(desc: *mut RxPipelineDesc, front_face: i32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *desc }.front_face = front_face;
}

/// # Safety
///
/// `desc` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_desc_set_depth_compare_op(desc: *mut RxPipelineDesc, op: i32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *desc }.depth_compare_op = op;
}

/// # Safety
///
/// `desc` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_desc_set_depth_test(desc: *mut RxPipelineDesc, value: bool)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *desc }.depth_test = value;
}

/// # Safety
///
/// `desc` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_desc_set_depth_write(desc: *mut RxPipelineDesc, value: bool)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *desc }.depth_write = value;
}

/// # Safety
///
/// `desc` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_desc_set_render_lines(desc: *mut RxPipelineDesc, value: bool)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *desc }.render_lines = value;
}

/// # Safety
///
/// `desc` must be live and `blend` valid.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_desc_add_blend(
	desc: *mut RxPipelineDesc,
	target_index: u32,
	blend: *const RxBlendAttachment,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *desc }.add_blend(target_index, blend_attachment(unsafe { &*blend }));
}

/// # Safety
///
/// `desc` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_desc_add_push_constants(
	desc: *mut RxPipelineDesc,
	size: u32,
	stages: u32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *desc }.push_constants.push((size, stages));
}

/// Adds a descriptor to a set. Entries that are not valid are left out.
///
/// # Safety
///
/// `desc` must be live and `entry` valid, referring to resources that live until the pipeline is
/// built.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_desc_add_entry(
	desc: *mut RxPipelineDesc,
	set: u32,
	entry: *const RxDescriptorEntry,
)
{
	// SAFETY: guaranteed by the caller.
	let (desc, entry) = unsafe { (&mut *desc, &*entry) };

	if let Some(entry) = entry_ref(entry) {
		desc.add_entry(set as usize, entry);
	}
}

/// # Safety
///
/// `desc` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_desc_set_features(desc: *mut RxPipelineDesc, features: u32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *desc }.features = features;
}

/// Sets what declares the descriptors of the description when the pipeline is built, with the
/// description. `free` is called with `user` when the declarer is dropped.
///
/// # Safety
///
/// `desc` must be live and `call` and `free` valid for `user`, with `call` allowed to run on the
/// thread that builds pipelines.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_desc_set_declare(
	desc: *mut RxPipelineDesc,
	call: DeclareFn,
	user: *mut c_void,
	free: Option<FreeFn>,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *desc }.declare = Some(unsafe { Declarer::new(call, user, free) });
}
