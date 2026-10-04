use ash::prelude::VkResult;
use ash::vk;

use crate::{AllocRequest, Allocation, Allocator, Device, Memory};

#[repr(u32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BufferType
{
	None,
	Storage,
	StorageWithOffset,
	Uniform,
	UniformWithOffset,
	Transfer,
	VertexBuffer,
	IndexBuffer,
}

impl BufferType
{
	pub fn from_raw(raw: u32) -> Option<Self>
	{
		Some(match raw {
			0 => Self::None,
			1 => Self::Storage,
			2 => Self::StorageWithOffset,
			3 => Self::Uniform,
			4 => Self::UniformWithOffset,
			5 => Self::Transfer,
			6 => Self::VertexBuffer,
			7 => Self::IndexBuffer,
			_ => return None,
		})
	}

	pub fn usage(self) -> vk::BufferUsageFlags
	{
		match self {
			Self::None | Self::Storage | Self::StorageWithOffset => {
				vk::BufferUsageFlags::STORAGE_BUFFER
			}
			Self::Uniform | Self::UniformWithOffset => vk::BufferUsageFlags::UNIFORM_BUFFER,
			Self::Transfer => vk::BufferUsageFlags::TRANSFER_SRC,
			Self::VertexBuffer => vk::BufferUsageFlags::VERTEX_BUFFER,
			Self::IndexBuffer => vk::BufferUsageFlags::INDEX_BUFFER,
		}
	}

	pub fn descriptor_type(self) -> vk::DescriptorType
	{
		match self {
			Self::Uniform | Self::UniformWithOffset => vk::DescriptorType::UNIFORM_BUFFER_DYNAMIC,
			_ => vk::DescriptorType::STORAGE_BUFFER_DYNAMIC,
		}
	}

	pub fn name(self) -> &'static std::ffi::CStr
	{
		match self {
			Self::None => c"None",
			Self::Storage => c"Storage",
			Self::StorageWithOffset => c"StorageWithOffset",
			Self::Uniform => c"Uniform",
			Self::UniformWithOffset => c"UniformWithOffset",
			Self::Transfer => c"Transfer",
			Self::VertexBuffer => c"VertexBuffer",
			Self::IndexBuffer => c"IndexBuffer",
		}
	}
}

pub const BUFFER_PERSISTENT_MAPPED: u16 = 1 << 0;
pub const BUFFER_TRANSFER_RECEIVER: u16 = 1 << 1;

pub struct Buffer
{
	pub handle: vk::Buffer,
	pub allocation: Allocation,
	pub mapped: *mut u8,
}

impl Buffer
{
	/// # Safety
	///
	/// The allocator must be live.
	pub unsafe fn create(
		allocator: &Allocator,
		ty: BufferType,
		size: u64,
		memory: Memory,
		flags: u16,
	) -> VkResult<Self>
	{
		let persistent = flags & BUFFER_PERSISTENT_MAPPED != 0;

		let mut usage = ty.usage();

		if flags & BUFFER_TRANSFER_RECEIVER != 0 {
			usage |= vk::BufferUsageFlags::TRANSFER_DST;
		}

		let info = vk::BufferCreateInfo::default()
			.size(size)
			.usage(usage)
			.sharing_mode(vk::SharingMode::EXCLUSIVE);

		let request = AllocRequest {
			memory,
			mapped: persistent,
			host_sequential_write: persistent,
			..Default::default()
		};

		// SAFETY: the create info outlives the call.
		let (handle, allocation, mapped) = unsafe { allocator.create_buffer(&info, &request) }?;

		Ok(Self {
			handle,
			allocation,
			mapped,
		})
	}
}

impl Device
{
	/// # Safety
	///
	/// `cmd` must be recording, and both buffers must be live with the right transfer usage.
	pub unsafe fn cmd_copy_buffer(
		&self,
		cmd: vk::CommandBuffer,
		src: vk::Buffer,
		dst: vk::Buffer,
		size: u64,
	)
	{
		let region = vk::BufferCopy::default().size(size);

		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().cmd_copy_buffer(cmd, src, dst, &[region]) };
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn raw_values_round_trip()
	{
		for raw in 0..8 {
			assert_eq!(BufferType::from_raw(raw).map(|ty| ty as u32), Some(raw));
		}

		assert!(BufferType::from_raw(8).is_none());
	}

	#[test]
	fn usage_and_descriptor_types()
	{
		assert_eq!(
			BufferType::None.usage(),
			vk::BufferUsageFlags::STORAGE_BUFFER
		);
		assert_eq!(
			BufferType::Transfer.usage(),
			vk::BufferUsageFlags::TRANSFER_SRC
		);
		assert_eq!(
			BufferType::IndexBuffer.usage(),
			vk::BufferUsageFlags::INDEX_BUFFER
		);

		assert_eq!(
			BufferType::UniformWithOffset.descriptor_type(),
			vk::DescriptorType::UNIFORM_BUFFER_DYNAMIC
		);
		assert_eq!(
			BufferType::VertexBuffer.descriptor_type(),
			vk::DescriptorType::STORAGE_BUFFER_DYNAMIC
		);
		assert_eq!(
			BufferType::StorageWithOffset.name().to_str(),
			Ok("StorageWithOffset")
		);
	}
}
