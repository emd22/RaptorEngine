use std::sync::{Mutex, MutexGuard};

use ash::vk::{self, Handle};
use raptor_gfx::{CommandBuffer, Gfx};
use raptor_shader::{Log, LogLevel};

use crate::names::{PipelineHandle, PipelineName, PipelinePass};
use crate::pipeline_cache::{
	BuildFailure, Builder, GpuBuilder, PipelineCache, PipelineSlot, TemplateFn,
};
use crate::pipeline_desc::PipelineDesc;
use crate::shader_library::ShaderLibrary;

pub const MAX_DYNAMIC_PIPELINES: u32 = 256;

struct CoreLog;

impl Log for CoreLog {
	fn log(&mut self, level: LogLevel, message: &str) {
		match level {
			LogLevel::Print => raptor_core::log_info!(Shader; "{message}"),
			LogLevel::Warning => raptor_core::log_warn!(Shader; "{message}"),
			LogLevel::Error => raptor_core::log_error!(Shader; "{message}"),
		}
	}
}

pub struct Pipelines {
	cache: PipelineCache,
	library: Mutex<ShaderLibrary>,
}

fn report(failure: BuildFailure) {
	match failure {
		BuildFailure::DescriptorMismatch => {
			raptor_core::log_fatal!(Render; "PSOBuild: Descriptor mismatch");
			panic!("PSOBuild: Descriptor mismatch");
		}
		BuildFailure::Vulkan(result) => {
			raptor_core::log_fatal!(Render; "Could not create pipeline: {result}");
			panic!("Could not create pipeline: {result}");
		}
		BuildFailure::BlendTarget => {
			raptor_core::log_fatal!(Render; "A blend attachment targets an attachment that does not exist");
			panic!("A blend attachment targets an attachment that does not exist");
		}
		BuildFailure::ShaderCompile | BuildFailure::ShaderCompilerUnavailable => {
			raptor_core::log_fatal!(Shader; "Error compiling shaders");
			panic!("Error compiling shaders");
		}
	}
}

impl Pipelines {
	pub fn new(shader_directory: &str) -> Self {
		Self {
			cache: PipelineCache::new(crate::names::NUM_PIPELINES, MAX_DYNAMIC_PIPELINES),
			library: Mutex::new(ShaderLibrary::new(shader_directory)),
		}
	}

	pub fn cache(&self) -> &PipelineCache {
		&self.cache
	}

	fn library(&self) -> MutexGuard<'_, ShaderLibrary> {
		self.library
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
	}

	fn run<R>(&self, gfx: &Gfx, f: impl FnOnce(&mut dyn Builder) -> R) -> R {
		let mut library = self.library();
		let mut descriptors = gfx.descriptors();
		let mut log = CoreLog;

		let mut builder = GpuBuilder {
			device: gfx.device(),
			descriptors: &mut descriptors,
			ds_layouts: gfx.ds_layouts(),
			library: &mut library,
			log: &mut log,
		};

		f(&mut builder)
	}

	pub fn build_static(&self, gfx: &Gfx, name: PipelineName, mut desc: PipelineDesc) {
		desc.debug_name = name.name().to_owned();

		self.cache.set_static_name(name as u32, name.name());

		let result = self.run(gfx, |builder| {
			self.cache.build_static(name as u32, desc, builder)
		});

		if let Err(failure) = result {
			report(failure);
		}
	}

	pub fn register_template(&self, pass: PipelinePass, template: TemplateFn) {
		self.cache.register_template(pass as u32, template);
	}

	pub fn get_or_create_variant(
		&self,
		gfx: &Gfx,
		pass: PipelinePass,
		features: crate::names::Features,
	) -> PipelineHandle {
		let result = self.run(gfx, |builder| {
			self.cache
				.get_or_create_variant(pass as u32, features.0, builder)
		});

		match result {
			Ok(handle) => PipelineHandle(handle),
			Err(failure) => {
				report(failure);
				PipelineHandle::INVALID
			}
		}
	}

	pub fn get_or_create_variant_in_pass(
		&self,
		gfx: &Gfx,
		handle: PipelineHandle,
		pass: PipelinePass,
	) -> PipelineHandle {
		let result = self.run(gfx, |builder| {
			self.cache
				.get_or_create_variant_in_pass(handle.0, pass as u32, builder)
		});

		match result {
			Ok(variant) => PipelineHandle(variant),
			Err(failure) => {
				report(failure);
				PipelineHandle::INVALID
			}
		}
	}

	pub fn find_variant(
		&self,
		pass: PipelinePass,
		features: crate::names::Features,
	) -> PipelineHandle {
		PipelineHandle(self.cache.find_variant(pass as u32, features.0))
	}

	pub fn find_variant_in_pass(
		&self,
		handle: PipelineHandle,
		pass: PipelinePass,
	) -> PipelineHandle {
		PipelineHandle(self.cache.find_variant_in_pass(handle.0, pass as u32))
	}

	pub fn pass_pipelines(&self, pass: PipelinePass) -> Vec<PipelineHandle> {
		(0..)
			.map_while(|index| self.cache.registry().pass_pipeline(pass as u32, index))
			.map(PipelineHandle)
			.collect()
	}

	pub fn build_pending(&self, gfx: &Gfx) {
		if let Err(failure) = self.run(gfx, |builder| self.cache.build_pending(builder)) {
			report(failure);
		}
	}

	pub fn slot(&self, gfx: &Gfx, handle: PipelineHandle) -> Option<&PipelineSlot> {
		if handle.0 >= self.cache.num_static()
			&& self.cache.has_pending()
			&& self.cache.is_main_thread()
		{
			self.build_pending(gfx);
		}

		let slot = self.cache.slot(handle.0);

		// SAFETY: slots live until the cache is destroyed and are only replaced on the thread that
		// builds pipelines.
		unsafe { slot.as_ref() }
	}

	pub fn is_built(&self, gfx: &Gfx, handle: PipelineHandle) -> bool {
		self.slot(gfx, handle).is_some_and(PipelineSlot::is_built)
	}

	pub fn debug_name(&self, handle: PipelineHandle) -> String {
		// SAFETY: as in `slot`.
		unsafe { self.cache.slot(handle.0).as_ref() }
			.map(|slot| {
				// SAFETY: the name pointer points into the slot's own string.
				unsafe { std::ffi::CStr::from_ptr(slot.debug_name) }
					.to_string_lossy()
					.into_owned()
			})
			.unwrap_or_else(|| "Unknown".to_owned())
	}

	pub fn add_buffer_offset(&self, set: usize, offset: u32) {
		self.cache.add_buffer_offset(set, offset);
	}

	pub fn bind_pipeline(&self, gfx: &Gfx, cmd: &CommandBuffer, slot: &PipelineSlot) {
		if cmd.bound_pipeline() == slot.pipeline {
			return;
		}

		let bind_point = bind_point(slot);

		// SAFETY: the command buffer is recording and the pipeline is built.
		unsafe {
			gfx.device().cmd_bind_pipeline(
				cmd.raw(),
				bind_point,
				vk::Pipeline::from_raw(slot.pipeline),
			);

			if slot.is_compute == 0 {
				gfx.device().cmd_set_cull_mode(
					cmd.raw(),
					vk::CullModeFlags::from_raw(slot.default_cull_mode),
				);
			}
		}

		cmd.set_bound_pipeline(slot.pipeline);
	}

	pub fn set_double_sided(
		&self,
		gfx: &Gfx,
		cmd: &CommandBuffer,
		slot: &PipelineSlot,
		double_sided: bool,
	) {
		if slot.is_compute != 0 {
			return;
		}

		let mode = if double_sided {
			0
		} else {
			slot.default_cull_mode
		};

		// SAFETY: the command buffer is recording with a graphics pipeline bound.
		unsafe {
			gfx.device()
				.cmd_set_cull_mode(cmd.raw(), vk::CullModeFlags::from_raw(mode))
		};
	}

	pub fn bind_sets(&self, gfx: &Gfx, cmd: &CommandBuffer, slot: &PipelineSlot) -> bool {
		let offsets = self.cache.take_offsets();

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

			// SAFETY: the command buffer is recording and the layout is the pipeline's own.
			unsafe {
				record.bind(
					gfx.device(),
					cmd.raw(),
					bind_point(slot),
					vk::PipelineLayout::from_raw(slot.layout),
					set.index,
					set_offsets,
				)
			};
		}

		matched
	}

	pub fn bind(&self, gfx: &Gfx, cmd: &CommandBuffer, handle: PipelineHandle) {
		let Some(slot) = self.slot(gfx, handle) else {
			return;
		};

		if !slot.is_built() {
			return;
		}

		self.bind_pipeline(gfx, cmd, slot);

		let matched = self.bind_sets(gfx, cmd, slot);

		assert!(
			matched,
			"The buffer offsets do not match the buffers in the pipeline's descriptor sets"
		);
	}

	pub fn bind_named(&self, gfx: &Gfx, cmd: &CommandBuffer, name: PipelineName) {
		self.bind(gfx, cmd, name.handle());
	}

	pub fn push_constants(
		&self,
		gfx: &Gfx,
		cmd: &CommandBuffer,
		slot: &PipelineSlot,
		stages: vk::ShaderStageFlags,
		data: &[u8],
	) {
		gfx.cmd_push_constants(
			cmd.raw(),
			vk::PipelineLayout::from_raw(slot.layout),
			stages,
			data,
		);
	}

	pub fn destroy(self, gfx: &Gfx) {
		let library = self
			.library
			.into_inner()
			.unwrap_or_else(|poisoned| poisoned.into_inner());

		gfx.wait_idle();

		// SAFETY: the device is idle and the library's programs were made with it.
		unsafe { library.destroy(gfx.device()) };

		self.cache.destroy(Some(gfx.device()));
	}
}

fn bind_point(slot: &PipelineSlot) -> vk::PipelineBindPoint {
	if slot.is_compute != 0 {
		vk::PipelineBindPoint::COMPUTE
	} else {
		vk::PipelineBindPoint::GRAPHICS
	}
}
