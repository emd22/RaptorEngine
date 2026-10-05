use std::ffi::{CString, c_char};
use std::sync::Arc;

use ash::prelude::VkResult;
use ash::vk::{self, Handle};

use crate::{Device, ReflectionEntry};

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct ShaderProgramFields
{
	pub module: u64,
	pub reflection: *const ReflectionEntry,
	pub reflection_count: usize,
	pub name: *const c_char,
	pub shader_type: u32,
	pub input_location_mask: u32,
}

#[repr(C)]
pub struct ShaderProgramRecord
{
	pub fields: ShaderProgramFields,
	reflection: Vec<ReflectionEntry>,
	name: CString,
}

impl ShaderProgramRecord
{
	#[allow(clippy::arc_with_non_send_sync)]
	pub fn create(
		device: &Device,
		code: &[u32],
		reflection: Vec<ReflectionEntry>,
		shader_type: u32,
		input_location_mask: u32,
		name: &str,
	) -> VkResult<Arc<Self>>
	{
		let module = device.create_shader_module(code)?;

		let name = CString::new(name.replace('\0', "")).unwrap_or_default();

		Ok(Arc::new(Self {
			fields: ShaderProgramFields {
				module: module.as_raw(),
				reflection: reflection.as_ptr(),
				reflection_count: reflection.len(),
				name: name.as_ptr(),
				shader_type,
				input_location_mask,
			},
			reflection,
			name,
		}))
	}

	pub fn reflection(&self) -> &[ReflectionEntry]
	{
		&self.reflection
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

	/// Drops a reference held as an `Arc`, destroying the module if it was the last one.
	///
	/// # Safety
	///
	/// As for `release`.
	pub unsafe fn release_arc(this: Arc<Self>, device: &Device) -> bool
	{
		let Some(record) = Arc::into_inner(this) else {
			return false;
		};

		// SAFETY: guaranteed by the caller, and the last reference is gone.
		unsafe { device.destroy_shader_module(vk::ShaderModule::from_raw(record.fields.module)) };

		true
	}

	/// # Safety
	///
	/// `record` must come from `into_raw` and not be used by the caller afterwards. The device must
	/// be the one the module was created with, or `None` to leak it, and no pipeline creation
	/// may be using the module.
	pub unsafe fn release(record: *const Self, device: Option<&Device>) -> bool
	{
		// SAFETY: guaranteed by the caller.
		let Some(record) = Arc::into_inner(unsafe { Arc::from_raw(record) }) else {
			return false;
		};

		if let Some(device) = device {
			// SAFETY: guaranteed by the caller, and the last reference is gone.
			unsafe {
				device.destroy_shader_module(vk::ShaderModule::from_raw(record.fields.module))
			};
		}

		true
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
		assert_eq!(offset_of!(ShaderProgramRecord, fields), 0);
		assert_eq!(size_of::<ReflectionEntry>(), 4);
		assert_eq!(size_of::<ShaderProgramFields>(), 40);
		assert_eq!(offset_of!(ShaderProgramFields, module), 0);
		assert_eq!(offset_of!(ShaderProgramFields, reflection), 8);
		assert_eq!(offset_of!(ShaderProgramFields, reflection_count), 16);
		assert_eq!(offset_of!(ShaderProgramFields, name), 24);
		assert_eq!(offset_of!(ShaderProgramFields, shader_type), 32);
		assert_eq!(offset_of!(ShaderProgramFields, input_location_mask), 36);
	}
}
