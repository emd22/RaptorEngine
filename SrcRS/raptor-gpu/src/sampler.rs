use std::collections::HashMap;
use std::sync::Mutex;

use ash::prelude::VkResult;
use ash::vk;

use crate::{Device, Level};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Filter
{
	Nearest,
	Linear,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum AddressMode
{
	Repeat,
	ClampToBorder,
	ClampToEdge,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum BorderColor
{
	IntBlack,
	FloatBlack,
	IntWhite,
	FloatWhite,
	IntTransparent,
	FloatTransparent,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum CompareOp
{
	None,
	Equal,
	Less,
	Greater,
	LessOrEqual,
	GreaterOrEqual,
}

impl Filter
{
	pub fn from_raw(raw: u8) -> Self
	{
		match raw {
			0 => Self::Nearest,
			_ => Self::Linear,
		}
	}

	fn filter(self) -> vk::Filter
	{
		match self {
			Self::Nearest => vk::Filter::NEAREST,
			Self::Linear => vk::Filter::LINEAR,
		}
	}

	fn mipmap_mode(self) -> vk::SamplerMipmapMode
	{
		match self {
			Self::Nearest => vk::SamplerMipmapMode::NEAREST,
			Self::Linear => vk::SamplerMipmapMode::LINEAR,
		}
	}
}

impl AddressMode
{
	pub fn from_raw(raw: u8) -> Self
	{
		match raw {
			1 => Self::ClampToBorder,
			2 => Self::ClampToEdge,
			_ => Self::Repeat,
		}
	}

	fn to_vk(self) -> vk::SamplerAddressMode
	{
		match self {
			Self::Repeat => vk::SamplerAddressMode::REPEAT,
			Self::ClampToBorder => vk::SamplerAddressMode::CLAMP_TO_BORDER,
			Self::ClampToEdge => vk::SamplerAddressMode::CLAMP_TO_EDGE,
		}
	}
}

impl BorderColor
{
	pub fn from_raw(raw: u8) -> Self
	{
		match raw {
			1 => Self::FloatBlack,
			2 => Self::IntWhite,
			3 => Self::FloatWhite,
			4 => Self::IntTransparent,
			5 => Self::FloatTransparent,
			_ => Self::IntBlack,
		}
	}

	fn to_vk(self) -> vk::BorderColor
	{
		match self {
			Self::IntBlack => vk::BorderColor::INT_OPAQUE_BLACK,
			Self::FloatBlack => vk::BorderColor::FLOAT_OPAQUE_BLACK,
			Self::IntWhite => vk::BorderColor::INT_OPAQUE_WHITE,
			Self::FloatWhite => vk::BorderColor::FLOAT_OPAQUE_WHITE,
			Self::IntTransparent => vk::BorderColor::INT_TRANSPARENT_BLACK,
			Self::FloatTransparent => vk::BorderColor::FLOAT_TRANSPARENT_BLACK,
		}
	}
}

impl CompareOp
{
	pub fn from_raw(raw: u8) -> Self
	{
		match raw {
			1 => Self::Equal,
			2 => Self::Less,
			3 => Self::Greater,
			4 => Self::LessOrEqual,
			5 => Self::GreaterOrEqual,
			_ => Self::None,
		}
	}

	fn to_vk(self) -> vk::CompareOp
	{
		match self {
			Self::None => vk::CompareOp::NEVER,
			Self::Equal => vk::CompareOp::EQUAL,
			Self::Less => vk::CompareOp::LESS,
			Self::Greater => vk::CompareOp::GREATER,
			Self::LessOrEqual => vk::CompareOp::LESS_OR_EQUAL,
			Self::GreaterOrEqual => vk::CompareOp::GREATER_OR_EQUAL,
		}
	}
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SamplerProps
{
	pub min_filter: Filter,
	pub mag_filter: Filter,
	pub mip_filter: Filter,
	pub address_mode: AddressMode,
	pub border_color: BorderColor,
	pub compare_op: CompareOp,
	pub max_anisotropy: u8,
	pub min_lod: f32,
	pub max_lod: f32,
}

impl Default for SamplerProps
{
	fn default() -> Self
	{
		Self {
			min_filter: Filter::Linear,
			mag_filter: Filter::Linear,
			mip_filter: Filter::Linear,
			address_mode: AddressMode::Repeat,
			border_color: BorderColor::IntBlack,
			compare_op: CompareOp::None,
			max_anisotropy: 1,
			min_lod: 0.0,
			max_lod: 0.0,
		}
	}
}

#[derive(Clone, Copy, PartialEq, Eq, Hash)]
struct SamplerKey
{
	filters: [Filter; 3],
	address_mode: AddressMode,
	border_color: BorderColor,
	compare_op: CompareOp,
	max_anisotropy: u8,
	min_lod: u32,
	max_lod: u32,
}

impl From<&SamplerProps> for SamplerKey
{
	fn from(props: &SamplerProps) -> Self
	{
		Self {
			filters: [props.min_filter, props.mag_filter, props.mip_filter],
			address_mode: props.address_mode,
			border_color: props.border_color,
			compare_op: props.compare_op,
			max_anisotropy: props.max_anisotropy,
			min_lod: props.min_lod.to_bits(),
			max_lod: props.max_lod.to_bits(),
		}
	}
}

impl Device
{
	pub fn create_sampler(&self, props: &SamplerProps) -> VkResult<vk::Sampler>
	{
		let address_mode = props.address_mode.to_vk();
		let max_anisotropy =
			f32::from(props.max_anisotropy).min(self.caps().max_sampler_anisotropy);

		let info = vk::SamplerCreateInfo::default()
			.mag_filter(props.mag_filter.filter())
			.min_filter(props.min_filter.filter())
			.mipmap_mode(props.mip_filter.mipmap_mode())
			.address_mode_u(address_mode)
			.address_mode_v(address_mode)
			.address_mode_w(address_mode)
			.mip_lod_bias(0.0)
			.anisotropy_enable(max_anisotropy > 1.0)
			.max_anisotropy(max_anisotropy.max(1.0))
			.compare_enable(props.compare_op != CompareOp::None)
			.compare_op(props.compare_op.to_vk())
			.min_lod(props.min_lod)
			.max_lod(props.max_lod)
			.border_color(props.border_color.to_vk())
			.unnormalized_coordinates(false);

		// SAFETY: the device is alive.
		unsafe { self.raw().create_sampler(&info, None) }
	}

	/// # Safety
	///
	/// The sampler must belong to this device and not be in use.
	pub unsafe fn destroy_sampler(&self, sampler: vk::Sampler)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().destroy_sampler(sampler, None) };
	}
}

#[repr(C)]
pub struct SamplerEntry
{
	pub handle: vk::Sampler,
}

#[derive(Default)]
pub struct SamplerCache
{
	entries: Mutex<HashMap<SamplerKey, Box<SamplerEntry>>>,
}

impl SamplerCache
{
	pub fn request(&self, device: &Device, props: &SamplerProps) -> *const SamplerEntry
	{
		let mut entries = self
			.entries
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner());

		let entry = entries.entry(SamplerKey::from(props)).or_insert_with(|| {
			let handle = device.create_sampler(props).unwrap_or_else(|_| {
				device
					.log()
					.log(Level::Error, "Error creating texture sampler!");
				vk::Sampler::null()
			});

			Box::new(SamplerEntry { handle })
		});

		&**entry
	}

	pub fn len(&self) -> usize
	{
		self.entries
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.len()
	}

	pub fn is_empty(&self) -> bool
	{
		self.len() == 0
	}

	/// # Safety
	///
	/// The device must be the one the samplers were created on, and none of them may be in use.
	/// Entry pointers handed out by `request` are invalid afterwards.
	pub unsafe fn destroy(&self, device: &Device)
	{
		let mut entries = self
			.entries
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner());

		for (_, entry) in entries.drain() {
			if entry.handle != vk::Sampler::null() {
				// SAFETY: guaranteed by the caller.
				unsafe { device.destroy_sampler(entry.handle) };
			}
		}
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn defaults_match_the_engine_defaults()
	{
		let props = SamplerProps::default();

		assert_eq!(props.min_filter, Filter::Linear);
		assert_eq!(props.address_mode, AddressMode::Repeat);
		assert_eq!(props.border_color, BorderColor::IntBlack);
		assert_eq!(props.compare_op, CompareOp::None);
		assert_eq!(props.max_anisotropy, 1);
	}

	#[test]
	fn raw_values_decode()
	{
		assert_eq!(Filter::from_raw(0), Filter::Nearest);
		assert_eq!(AddressMode::from_raw(2), AddressMode::ClampToEdge);
		assert_eq!(BorderColor::from_raw(5), BorderColor::FloatTransparent);
		assert_eq!(CompareOp::from_raw(5), CompareOp::GreaterOrEqual);
		assert_eq!(CompareOp::from_raw(200), CompareOp::None);
	}

	#[test]
	fn float_white_is_float_white()
	{
		assert_eq!(
			BorderColor::FloatWhite.to_vk(),
			vk::BorderColor::FLOAT_OPAQUE_WHITE
		);
		assert_eq!(
			BorderColor::IntWhite.to_vk(),
			vk::BorderColor::INT_OPAQUE_WHITE
		);
	}

	#[test]
	fn no_compare_maps_to_never()
	{
		assert_eq!(CompareOp::None.to_vk(), vk::CompareOp::NEVER);
		assert_eq!(CompareOp::Greater.to_vk(), vk::CompareOp::GREATER);
	}

	#[test]
	fn keys_separate_every_field()
	{
		let base = SamplerProps::default();
		let key = SamplerKey::from(&base);

		assert!(key == SamplerKey::from(&base));

		let variants = [
			SamplerProps {
				min_filter: Filter::Nearest,
				..base
			},
			SamplerProps {
				mip_filter: Filter::Nearest,
				..base
			},
			SamplerProps {
				address_mode: AddressMode::ClampToEdge,
				..base
			},
			SamplerProps {
				border_color: BorderColor::FloatWhite,
				..base
			},
			SamplerProps {
				compare_op: CompareOp::Greater,
				..base
			},
			SamplerProps {
				max_anisotropy: 8,
				..base
			},
			SamplerProps {
				max_lod: 5.0,
				..base
			},
		];

		for variant in &variants {
			assert!(key != SamplerKey::from(variant));
		}
	}
}
