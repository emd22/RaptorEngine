use ash::vk;

#[repr(u16)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ImageFormat
{
	None,
	Bgra8UNorm,
	Bgra8Srgb,
	Rgba8Srgb,
	Rgba8UNorm,
	Rg32Float,
	Rg16UNorm,
	Rgba16Float,
	Rgb32Float,
	D16UNormS8UInt,
	D32Float,
	D32FloatS8UInt,
	R32SFloat,
	R32SInt,
	R32UInt,
	R8UInt,
	R8UNorm,
}

impl ImageFormat
{
	pub fn from_raw(raw: u16) -> Option<Self>
	{
		const ALL: [ImageFormat; 17] = [
			ImageFormat::None,
			ImageFormat::Bgra8UNorm,
			ImageFormat::Bgra8Srgb,
			ImageFormat::Rgba8Srgb,
			ImageFormat::Rgba8UNorm,
			ImageFormat::Rg32Float,
			ImageFormat::Rg16UNorm,
			ImageFormat::Rgba16Float,
			ImageFormat::Rgb32Float,
			ImageFormat::D16UNormS8UInt,
			ImageFormat::D32Float,
			ImageFormat::D32FloatS8UInt,
			ImageFormat::R32SFloat,
			ImageFormat::R32SInt,
			ImageFormat::R32UInt,
			ImageFormat::R8UInt,
			ImageFormat::R8UNorm,
		];

		ALL.get(usize::from(raw)).copied()
	}

	pub fn is_depth(self) -> bool
	{
		matches!(
			self,
			Self::D16UNormS8UInt | Self::D32Float | Self::D32FloatS8UInt
		)
	}

	pub fn is_stencil(self) -> bool
	{
		matches!(self, Self::D16UNormS8UInt | Self::D32FloatS8UInt)
	}

	pub fn is_srgb(self) -> bool
	{
		matches!(self, Self::Bgra8Srgb | Self::Rgba8Srgb)
	}

	pub fn pixel_stride(self) -> u32
	{
		match self {
			Self::None => 0,
			Self::Bgra8UNorm
			| Self::Bgra8Srgb
			| Self::Rgba8Srgb
			| Self::Rgba8UNorm
			| Self::Rg16UNorm
			| Self::R32SFloat
			| Self::R32SInt
			| Self::R32UInt
			| Self::D32Float => 4,
			Self::Rg32Float | Self::Rgba16Float => 8,
			Self::Rgb32Float => 12,
			Self::D16UNormS8UInt => 3,
			Self::D32FloatS8UInt => 5,
			Self::R8UInt | Self::R8UNorm => 1,
		}
	}

	pub fn aspect_mask(self) -> vk::ImageAspectFlags
	{
		let mut aspect = vk::ImageAspectFlags::empty();

		if self.is_depth() {
			aspect |= vk::ImageAspectFlags::DEPTH;
		}

		if self.is_stencil() {
			aspect |= vk::ImageAspectFlags::STENCIL;
		}

		if aspect.is_empty() {
			vk::ImageAspectFlags::COLOR
		} else {
			aspect
		}
	}

	pub fn usage_flags(self) -> vk::ImageUsageFlags
	{
		if self.is_depth() || self.is_stencil() {
			vk::ImageUsageFlags::DEPTH_STENCIL_ATTACHMENT
		} else {
			vk::ImageUsageFlags::COLOR_ATTACHMENT
		}
	}

	pub fn to_vk(self) -> vk::Format
	{
		match self {
			Self::None => vk::Format::UNDEFINED,
			Self::Bgra8UNorm => vk::Format::B8G8R8A8_UNORM,
			Self::Bgra8Srgb => vk::Format::B8G8R8A8_SRGB,
			Self::Rgba8Srgb => vk::Format::R8G8B8A8_SRGB,
			Self::Rgba8UNorm => vk::Format::R8G8B8A8_UNORM,
			Self::Rg32Float => vk::Format::R32G32_SFLOAT,
			Self::Rg16UNorm => vk::Format::R16G16_UNORM,
			Self::Rgba16Float => vk::Format::R16G16B16A16_SFLOAT,
			Self::Rgb32Float => vk::Format::R32G32B32_SFLOAT,
			Self::D16UNormS8UInt => vk::Format::D16_UNORM_S8_UINT,
			Self::D32Float => vk::Format::D32_SFLOAT,
			Self::D32FloatS8UInt => vk::Format::D32_SFLOAT_S8_UINT,
			Self::R32SFloat => vk::Format::R32_SFLOAT,
			Self::R32SInt => vk::Format::R32_SINT,
			Self::R32UInt => vk::Format::R32_UINT,
			Self::R8UInt => vk::Format::R8_UINT,
			Self::R8UNorm => vk::Format::R8_UNORM,
		}
	}
}

#[repr(u32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ImageType
{
	Flat,
	Cubemap,
	CubemapArray,
}

impl ImageType
{
	pub fn from_raw(raw: u32) -> Option<Self>
	{
		match raw {
			0 => Some(Self::Flat),
			1 => Some(Self::Cubemap),
			2 => Some(Self::CubemapArray),
			_ => None,
		}
	}

	pub fn is_cube(self) -> bool
	{
		!matches!(self, Self::Flat)
	}

	pub fn properties(self, cube_count: u32) -> (vk::ImageViewType, u32)
	{
		match self {
			Self::Flat => (vk::ImageViewType::TYPE_2D, 1),
			Self::Cubemap => (vk::ImageViewType::CUBE, 6),
			Self::CubemapArray => (vk::ImageViewType::CUBE_ARRAY, 6 * cube_count.max(1)),
		}
	}
}

pub fn mip_dimensions(size: (u32, u32), level: u32) -> (u32, u32)
{
	let divisor = match 1u32.checked_shl(level) {
		Some(step) => 1.0 / step as f32,
		None => 0.0,
	};

	let scale = |extent: u32| ((extent as f32 * divisor) as u32).max(1);

	(scale(size.0), scale(size.1))
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn raw_values_round_trip()
	{
		for raw in 0..17 {
			assert_eq!(ImageFormat::from_raw(raw).map(|f| f as u16), Some(raw));
		}

		assert!(ImageFormat::from_raw(17).is_none());
	}

	#[test]
	fn strides_and_aspects()
	{
		assert_eq!(ImageFormat::Rgba8UNorm.pixel_stride(), 4);
		assert_eq!(ImageFormat::Rgba16Float.pixel_stride(), 8);
		assert_eq!(ImageFormat::Rgb32Float.pixel_stride(), 12);
		assert_eq!(ImageFormat::D32FloatS8UInt.pixel_stride(), 5);
		assert_eq!(ImageFormat::None.pixel_stride(), 0);

		assert_eq!(
			ImageFormat::Rgba8UNorm.aspect_mask(),
			vk::ImageAspectFlags::COLOR
		);
		assert_eq!(
			ImageFormat::D32Float.aspect_mask(),
			vk::ImageAspectFlags::DEPTH
		);
		assert_eq!(
			ImageFormat::D16UNormS8UInt.aspect_mask(),
			vk::ImageAspectFlags::DEPTH | vk::ImageAspectFlags::STENCIL
		);
	}

	#[test]
	fn usage_follows_depth()
	{
		assert_eq!(
			ImageFormat::D32Float.usage_flags(),
			vk::ImageUsageFlags::DEPTH_STENCIL_ATTACHMENT
		);
		assert_eq!(
			ImageFormat::Bgra8Srgb.usage_flags(),
			vk::ImageUsageFlags::COLOR_ATTACHMENT
		);
		assert!(ImageFormat::Bgra8Srgb.is_srgb());
		assert!(!ImageFormat::Bgra8UNorm.is_srgb());
	}

	#[test]
	fn image_type_properties()
	{
		assert_eq!(
			ImageType::Flat.properties(9),
			(vk::ImageViewType::TYPE_2D, 1)
		);
		assert_eq!(
			ImageType::Cubemap.properties(9),
			(vk::ImageViewType::CUBE, 6)
		);
		assert_eq!(
			ImageType::CubemapArray.properties(3),
			(vk::ImageViewType::CUBE_ARRAY, 18)
		);
		assert_eq!(ImageType::CubemapArray.properties(0).1, 6);
	}

	#[test]
	fn mip_dimensions_halve_and_clamp()
	{
		assert_eq!(mip_dimensions((256, 128), 0), (256, 128));
		assert_eq!(mip_dimensions((256, 128), 1), (128, 64));
		assert_eq!(mip_dimensions((256, 128), 3), (32, 16));
		assert_eq!(mip_dimensions((256, 128), 9), (1, 1));
		assert_eq!(mip_dimensions((5, 3), 1), (2, 1));
		assert_eq!(mip_dimensions((5, 3), 40), (1, 1));
	}
}
