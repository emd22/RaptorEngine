use std::mem::{offset_of, size_of};

use ash::vk;

#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct SlimVertex
{
	pub position: [f32; 3],
}

#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct DefaultVertex
{
	pub position: [f32; 3],
	pub normal: [f32; 3],
	pub uv: [f32; 2],
	pub tangent: [f32; 4],
}

#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct SkinnedVertex
{
	pub position: [f32; 3],
	pub normal: [f32; 3],
	pub uv: [f32; 2],
	pub tangent: [f32; 4],
	pub bone_ids: [u32; 4],
	pub bone_weights: [f32; 4],
}

pub const SLIM_SIZE: u32 = size_of::<SlimVertex>() as u32;
pub const DEFAULT_SIZE: u32 = size_of::<DefaultVertex>() as u32;
pub const SKINNED_SIZE: u32 = size_of::<SkinnedVertex>() as u32;

pub const NORMAL_OFFSET: u32 = offset_of!(DefaultVertex, normal) as u32;
pub const UV_OFFSET: u32 = offset_of!(DefaultVertex, uv) as u32;
pub const TANGENT_OFFSET: u32 = offset_of!(DefaultVertex, tangent) as u32;
pub const BONE_IDS_OFFSET: u32 = offset_of!(SkinnedVertex, bone_ids) as u32;
pub const BONE_WEIGHTS_OFFSET: u32 = offset_of!(SkinnedVertex, bone_weights) as u32;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum VertexType
{
	Slim,
	Default,
	Skinned,
}

impl VertexType
{
	pub fn from_raw(raw: u32) -> Option<Self>
	{
		match raw {
			0 => Some(Self::Slim),
			1 => Some(Self::Default),
			2 => Some(Self::Skinned),
			_ => None,
		}
	}

	pub fn stride(self) -> u32
	{
		match self {
			Self::Slim => SLIM_SIZE,
			Self::Default => DEFAULT_SIZE,
			Self::Skinned => SKINNED_SIZE,
		}
	}

	pub fn binding(self) -> vk::VertexInputBindingDescription
	{
		vk::VertexInputBindingDescription {
			binding: 0,
			stride: self.stride(),
			input_rate: vk::VertexInputRate::VERTEX,
		}
	}

	pub fn attributes(self) -> Vec<vk::VertexInputAttributeDescription>
	{
		let attribute = |location, format, offset| vk::VertexInputAttributeDescription {
			location,
			binding: 0,
			format,
			offset,
		};

		let mut attributes = vec![attribute(0, vk::Format::R32G32B32_SFLOAT, 0)];

		if self == Self::Slim {
			return attributes;
		}

		attributes.push(attribute(1, vk::Format::R32G32B32_SFLOAT, NORMAL_OFFSET));
		attributes.push(attribute(2, vk::Format::R32G32_SFLOAT, UV_OFFSET));
		attributes.push(attribute(
			3,
			vk::Format::R32G32B32A32_SFLOAT,
			TANGENT_OFFSET,
		));

		if self == Self::Skinned {
			attributes.push(attribute(4, vk::Format::R32G32B32A32_UINT, BONE_IDS_OFFSET));
			attributes.push(attribute(
				5,
				vk::Format::R32G32B32A32_SFLOAT,
				BONE_WEIGHTS_OFFSET,
			));
		}

		attributes
	}
}

pub fn filter_by_input_mask(
	attributes: &[vk::VertexInputAttributeDescription],
	mask: u32,
) -> Vec<vk::VertexInputAttributeDescription>
{
	attributes
		.iter()
		.filter(|attribute| attribute.location < 32 && mask & (1 << attribute.location) != 0)
		.copied()
		.collect()
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn strides_are_the_packed_sizes()
	{
		assert_eq!(VertexType::Slim.stride(), 12);
		assert_eq!(VertexType::Default.stride(), 48);
		assert_eq!(VertexType::Skinned.stride(), 80);
	}

	#[test]
	fn offsets_follow_the_field_order()
	{
		assert_eq!(NORMAL_OFFSET, 12);
		assert_eq!(UV_OFFSET, 24);
		assert_eq!(TANGENT_OFFSET, 32);
		assert_eq!(BONE_IDS_OFFSET, 48);
		assert_eq!(BONE_WEIGHTS_OFFSET, 64);
	}

	#[test]
	fn attribute_counts_grow_with_the_type()
	{
		assert_eq!(VertexType::Slim.attributes().len(), 1);
		assert_eq!(VertexType::Default.attributes().len(), 4);
		assert_eq!(VertexType::Skinned.attributes().len(), 6);
	}

	#[test]
	fn attributes_stay_inside_the_vertex()
	{
		for ty in [VertexType::Slim, VertexType::Default, VertexType::Skinned] {
			let binding = ty.binding();

			for (index, attribute) in ty.attributes().iter().enumerate() {
				assert_eq!(attribute.location, index as u32);
				assert!(attribute.offset < binding.stride);
			}
		}
	}

	#[test]
	fn the_input_mask_keeps_only_consumed_locations()
	{
		let attributes = VertexType::Skinned.attributes();

		let kept = filter_by_input_mask(&attributes, 0b0000_1101);
		let locations: Vec<_> = kept.iter().map(|attribute| attribute.location).collect();
		assert_eq!(locations, vec![0, 2, 3]);

		assert_eq!(filter_by_input_mask(&attributes, !0).len(), 6);
		assert!(filter_by_input_mask(&attributes, 0).is_empty());
	}

	#[test]
	fn raw_types_decode()
	{
		assert_eq!(VertexType::from_raw(2), Some(VertexType::Skinned));
		assert_eq!(VertexType::from_raw(3), None);
	}
}
