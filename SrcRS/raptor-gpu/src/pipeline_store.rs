use std::sync::Arc;

use ash::prelude::VkResult;
use ash::vk::{self, Handle};

use crate::{Device, GraphicsPipelineDesc, PushConstantDef};

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct PipelineLayoutFields
{
	pub layout: u64,
}

#[repr(C)]
pub struct PipelineLayoutRecord
{
	pub fields: PipelineLayoutFields,
}

impl PipelineLayoutRecord
{
	pub fn create(
		device: &Device,
		set_layouts: &[vk::DescriptorSetLayout],
		push_constants: &[PushConstantDef],
	) -> VkResult<Arc<Self>>
	{
		let layout = device.create_pipeline_layout(set_layouts, push_constants)?;

		Ok(Arc::new(Self {
			fields: PipelineLayoutFields {
				layout: layout.as_raw(),
			},
		}))
	}

	pub fn into_raw(this: Arc<Self>) -> *mut Self
	{
		Arc::into_raw(this).cast_mut()
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
	/// `record` must come from `into_raw` and not be used by the caller afterwards. The device must
	/// be the one the layout was created with, or `None` to leak it, and the layout must not be
	/// in use when the last reference goes.
	pub unsafe fn release(record: *const Self, device: Option<&Device>) -> bool
	{
		// SAFETY: guaranteed by the caller.
		let Some(record) = Arc::into_inner(unsafe { Arc::from_raw(record) }) else {
			return false;
		};

		if let Some(device) = device {
			// SAFETY: guaranteed by the caller, and the last reference is gone.
			unsafe {
				device.destroy_pipeline_layout(vk::PipelineLayout::from_raw(record.fields.layout))
			};
		}

		true
	}
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct PipelineFields
{
	pub pipeline: u64,
	pub bind_point: i32,
	pub default_cull_mode: u32,
	pub is_compute: u8,
}

#[repr(C)]
pub struct PipelineRecord
{
	pub fields: PipelineFields,
}

impl PipelineRecord
{
	pub fn create_graphics(device: &Device, desc: &GraphicsPipelineDesc) -> VkResult<Box<Self>>
	{
		let pipeline = device.create_graphics_pipeline(desc)?;

		Ok(Box::new(Self {
			fields: PipelineFields {
				pipeline: pipeline.as_raw(),
				bind_point: vk::PipelineBindPoint::GRAPHICS.as_raw(),
				default_cull_mode: desc.cull_mode.as_raw(),
				is_compute: 0,
			},
		}))
	}

	pub fn create_compute(
		device: &Device,
		module: vk::ShaderModule,
		layout: vk::PipelineLayout,
	) -> VkResult<Box<Self>>
	{
		let pipeline = device.create_compute_pipeline(module, layout)?;

		Ok(Box::new(Self {
			fields: PipelineFields {
				pipeline: pipeline.as_raw(),
				bind_point: vk::PipelineBindPoint::COMPUTE.as_raw(),
				default_cull_mode: vk::CullModeFlags::NONE.as_raw(),
				is_compute: 1,
			},
		}))
	}

	/// # Safety
	///
	/// The device must be the one the pipeline was created with and the pipeline must not be in
	/// use.
	pub unsafe fn destroy(self, device: &Device)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { device.destroy_pipeline(vk::Pipeline::from_raw(self.fields.pipeline)) };
	}
}

#[cfg(test)]
mod tests
{
	use std::mem::{offset_of, size_of};

	use super::*;

	#[test]
	fn the_public_fields_have_the_layout_the_c_header_declares()
	{
		assert_eq!(offset_of!(PipelineLayoutRecord, fields), 0);
		assert_eq!(size_of::<PipelineLayoutFields>(), 8);

		assert_eq!(offset_of!(PipelineRecord, fields), 0);
		assert_eq!(size_of::<PipelineFields>(), 24);
		assert_eq!(offset_of!(PipelineFields, pipeline), 0);
		assert_eq!(offset_of!(PipelineFields, bind_point), 8);
		assert_eq!(offset_of!(PipelineFields, default_cull_mode), 12);
		assert_eq!(offset_of!(PipelineFields, is_compute), 16);
	}
}
