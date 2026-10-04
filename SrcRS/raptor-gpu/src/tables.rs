use ash::vk;

pub const SHADER_VERTEX: u32 = 1 << 0;
pub const SHADER_PIXEL: u32 = 1 << 1;
pub const SHADER_COMPUTE: u32 = 1 << 2;

pub fn shader_stage_flags(bits: u32) -> vk::ShaderStageFlags
{
	let mut flags = vk::ShaderStageFlags::empty();

	if bits & SHADER_VERTEX != 0 {
		flags |= vk::ShaderStageFlags::VERTEX;
	}

	if bits & SHADER_PIXEL != 0 {
		flags |= vk::ShaderStageFlags::FRAGMENT;
	}

	if bits & SHADER_COMPUTE != 0 {
		flags |= vk::ShaderStageFlags::COMPUTE;
	}

	flags
}

pub fn shader_bind_point(bits: u32) -> vk::PipelineBindPoint
{
	if bits == SHADER_COMPUTE {
		vk::PipelineBindPoint::COMPUTE
	} else {
		vk::PipelineBindPoint::GRAPHICS
	}
}

pub fn shader_type_name(bits: u32) -> &'static std::ffi::CStr
{
	match bits {
		SHADER_VERTEX => c"Vertex",
		SHADER_PIXEL => c"Pixel",
		SHADER_COMPUTE => c"Compute",
		_ => c"Unknown",
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ReflectionType
{
	StructuredBuffer,
	CBuffer,
	Texture,
}

impl ReflectionType
{
	pub fn from_raw(raw: u16) -> Option<Self>
	{
		match raw {
			0 => Some(Self::StructuredBuffer),
			1 => Some(Self::CBuffer),
			2 => Some(Self::Texture),
			_ => None,
		}
	}

	pub fn descriptor_type(this: Option<Self>) -> vk::DescriptorType
	{
		match this {
			Some(Self::StructuredBuffer) => vk::DescriptorType::STORAGE_BUFFER_DYNAMIC,
			Some(Self::CBuffer) => vk::DescriptorType::UNIFORM_BUFFER_DYNAMIC,
			Some(Self::Texture) | None => vk::DescriptorType::COMBINED_IMAGE_SAMPLER,
		}
	}

	pub fn requires_offset(this: Option<Self>) -> bool
	{
		matches!(this, Some(Self::StructuredBuffer | Self::CBuffer))
	}

	pub fn name(this: Option<Self>) -> &'static std::ffi::CStr
	{
		match this {
			Some(Self::StructuredBuffer) => c"StructuredBuffer",
			Some(Self::CBuffer) => c"CBuffer",
			Some(Self::Texture) => c"Texture",
			None => c"Unknown",
		}
	}
}

pub fn result_name(result: vk::Result) -> String
{
	match result.as_raw() {
		-1_000_174_001 => return "VK_ERROR_NOT_PERMITTED".to_owned(),
		1_000_483_000 => return "VK_PIPELINE_BINARY_MISSING_KHR".to_owned(),
		-1_000_483_000 => return "VK_ERROR_NOT_ENOUGH_SPACE_KHR".to_owned(),
		_ => {}
	}

	let name = format!("{result:?}");

	if name.chars().all(|c| c == '-' || c.is_ascii_digit()) {
		"Unhandled VkResult".to_owned()
	} else {
		format!("VK_{name}")
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn stage_flags_combine()
	{
		assert_eq!(shader_stage_flags(0), vk::ShaderStageFlags::empty());
		assert_eq!(
			shader_stage_flags(SHADER_VERTEX),
			vk::ShaderStageFlags::VERTEX
		);
		assert_eq!(
			shader_stage_flags(SHADER_VERTEX | SHADER_PIXEL),
			vk::ShaderStageFlags::VERTEX | vk::ShaderStageFlags::FRAGMENT
		);
	}

	#[test]
	fn only_a_lone_compute_stage_binds_to_compute()
	{
		assert_eq!(
			shader_bind_point(SHADER_COMPUTE),
			vk::PipelineBindPoint::COMPUTE
		);
		assert_eq!(
			shader_bind_point(SHADER_VERTEX),
			vk::PipelineBindPoint::GRAPHICS
		);
		assert_eq!(
			shader_bind_point(SHADER_VERTEX | SHADER_COMPUTE),
			vk::PipelineBindPoint::GRAPHICS
		);
	}

	#[test]
	fn type_names_cover_single_stages_only()
	{
		assert_eq!(shader_type_name(SHADER_PIXEL).to_str(), Ok("Pixel"));
		assert_eq!(
			shader_type_name(SHADER_VERTEX | SHADER_PIXEL).to_str(),
			Ok("Unknown")
		);
	}

	#[test]
	fn reflection_types_map_to_descriptors()
	{
		let storage = ReflectionType::from_raw(0);
		let cbuffer = ReflectionType::from_raw(1);
		let texture = ReflectionType::from_raw(2);
		let unknown = ReflectionType::from_raw(9);

		assert_eq!(
			ReflectionType::descriptor_type(storage),
			vk::DescriptorType::STORAGE_BUFFER_DYNAMIC
		);
		assert_eq!(
			ReflectionType::descriptor_type(cbuffer),
			vk::DescriptorType::UNIFORM_BUFFER_DYNAMIC
		);
		assert_eq!(
			ReflectionType::descriptor_type(unknown),
			vk::DescriptorType::COMBINED_IMAGE_SAMPLER
		);
		assert!(ReflectionType::requires_offset(storage));
		assert!(ReflectionType::requires_offset(cbuffer));
		assert!(!ReflectionType::requires_offset(texture));
		assert_eq!(ReflectionType::name(texture).to_str(), Ok("Texture"));
		assert_eq!(ReflectionType::name(unknown).to_str(), Ok("Unknown"));
	}

	#[test]
	fn result_names_match_the_old_strings()
	{
		assert_eq!(result_name(vk::Result::SUCCESS), "VK_SUCCESS");
		assert_eq!(
			result_name(vk::Result::ERROR_OUT_OF_DATE_KHR),
			"VK_ERROR_OUT_OF_DATE_KHR"
		);
		assert_eq!(
			result_name(vk::Result::ERROR_DEVICE_LOST),
			"VK_ERROR_DEVICE_LOST"
		);
		assert_eq!(result_name(vk::Result::SUBOPTIMAL_KHR), "VK_SUBOPTIMAL_KHR");
		assert_eq!(
			result_name(vk::Result::from_raw(-123456)),
			"Unhandled VkResult"
		);
		assert_eq!(
			result_name(vk::Result::from_raw(-1_000_174_001)),
			"VK_ERROR_NOT_PERMITTED"
		);
		assert_eq!(
			result_name(vk::Result::from_raw(1_000_483_000)),
			"VK_PIPELINE_BINARY_MISSING_KHR"
		);
		assert_eq!(
			result_name(vk::Result::from_raw(-1_000_483_000)),
			"VK_ERROR_NOT_ENOUGH_SPACE_KHR"
		);
	}
}
