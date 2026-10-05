use std::ffi::{CStr, c_char, c_void};

use ash::vk;
use raptor_gpu::{
	BlendAttachment, ReflectionType, VertexType, blend_states, result_name, shader_bind_point,
	shader_stage_flags, shader_type_name,
};

use crate::gpu::RxGpuDevice;

/// # Safety
///
/// `out_binding` must be writable for one `VkVertexInputBindingDescription` and `out_attributes`
/// for `capacity` `VkVertexInputAttributeDescription`s.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_vertex_description(
	vertex_type: u32,
	out_binding: *mut c_void,
	out_attributes: *mut c_void,
	capacity: usize,
) -> usize
{
	let Some(vertex_type) = VertexType::from_raw(vertex_type) else {
		return 0;
	};

	let attributes = vertex_type.attributes();

	if attributes.len() > capacity {
		return 0;
	}

	// SAFETY: guaranteed by the caller.
	unsafe {
		*out_binding.cast::<vk::VertexInputBindingDescription>() = vertex_type.binding();

		std::ptr::copy_nonoverlapping(
			attributes.as_ptr(),
			out_attributes.cast::<vk::VertexInputAttributeDescription>(),
			attributes.len(),
		);
	}

	attributes.len()
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxBlendAttachment
{
	pub enabled: u8,
	pub write_mask: u32,
	pub color_op: i32,
	pub alpha_op: i32,
	pub src_color: i32,
	pub dst_color: i32,
	pub src_alpha: i32,
	pub dst_alpha: i32,
	pub target_index: u32,
}

pub(crate) fn blend_attachment(attachment: &RxBlendAttachment) -> BlendAttachment
{
	BlendAttachment {
		enabled: attachment.enabled != 0,
		write_mask: vk::ColorComponentFlags::from_raw(attachment.write_mask),
		color_op: vk::BlendOp::from_raw(attachment.color_op),
		alpha_op: vk::BlendOp::from_raw(attachment.alpha_op),
		src_color: vk::BlendFactor::from_raw(attachment.src_color),
		dst_color: vk::BlendFactor::from_raw(attachment.dst_color),
		src_alpha: vk::BlendFactor::from_raw(attachment.src_alpha),
		dst_alpha: vk::BlendFactor::from_raw(attachment.dst_alpha),
		target_index: attachment.target_index,
	}
}

/// # Safety
///
/// `attachments` must be valid for `attachment_count` entries and `out` writable for `count`
/// `VkPipelineColorBlendAttachmentState`s.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_blend_states(
	attachments: *const RxBlendAttachment,
	attachment_count: usize,
	count: u32,
	out: *mut c_void,
) -> i32
{
	let attachments: Vec<_> = if attachment_count == 0 {
		Vec::new()
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(attachments, attachment_count) }
			.iter()
			.map(blend_attachment)
			.collect()
	};

	let Ok(states) = blend_states(&attachments, count) else {
		return 0;
	};

	if states.is_empty() {
		return 1;
	}

	// SAFETY: guaranteed by the caller.
	unsafe {
		std::ptr::copy_nonoverlapping(
			states.as_ptr(),
			out.cast::<vk::PipelineColorBlendAttachmentState>(),
			states.len(),
		)
	};

	1
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_shader_stage_flags(bits: u32) -> u32
{
	shader_stage_flags(bits).as_raw()
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_shader_bind_point(bits: u32) -> i32
{
	shader_bind_point(bits).as_raw()
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_shader_type_name(bits: u32) -> *const c_char
{
	shader_type_name(bits).as_ptr()
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_reflection_descriptor_type(raw: u16) -> i32
{
	ReflectionType::descriptor_type(ReflectionType::from_raw(raw)).as_raw()
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_reflection_requires_offset(raw: u16) -> u8
{
	u8::from(ReflectionType::requires_offset(ReflectionType::from_raw(
		raw,
	)))
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_reflection_name(raw: u16) -> *const c_char
{
	ReflectionType::name(ReflectionType::from_raw(raw)).as_ptr()
}

/// # Safety
///
/// `buffer` must be writable for `capacity` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_vk_result_name(
	result: i32,
	buffer: *mut c_char,
	capacity: usize,
) -> usize
{
	if capacity == 0 {
		return 0;
	}

	let name = result_name(vk::Result::from_raw(result));
	let length = name.len().min(capacity - 1);

	// SAFETY: guaranteed by the caller.
	unsafe {
		std::ptr::copy_nonoverlapping(name.as_ptr().cast::<c_char>(), buffer, length);
		*buffer.add(length) = 0;
	}

	length
}

/// # Safety
///
/// `device` must be live, `handle` a handle of the given object type on it, and `name`
/// NUL-terminated.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_set_object_name(
	device: *const RxGpuDevice,
	object_type: i32,
	handle: u64,
	name: *const c_char,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let (device, name) = unsafe { (&(*device).device, CStr::from_ptr(name)) };

	i32::from(device.set_object_name(vk::ObjectType::from_raw(object_type), handle, name))
}
