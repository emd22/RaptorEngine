use ash::prelude::VkResult;
use ash::vk;
use vk_mem::{
	Alloc, AllocationCreateFlags, AllocationCreateInfo, AllocatorCreateInfo, MemoryUsage,
};

use crate::{Device, Instance};

pub use vk_mem::Allocation;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Memory
{
	Auto,
	AutoPreferDevice,
	GpuOnly,
	CpuOnly,
	CpuToGpu,
	GpuToCpu,
}

#[derive(Clone, Copy, Debug)]
pub struct AllocRequest
{
	pub memory: Memory,
	pub mapped: bool,
	pub host_sequential_write: bool,
	pub dedicated: bool,
	pub priority: f32,
}

impl Default for AllocRequest
{
	fn default() -> Self
	{
		Self {
			memory: Memory::Auto,
			mapped: false,
			host_sequential_write: false,
			dedicated: false,
			priority: 0.0,
		}
	}
}

#[allow(deprecated)]
fn create_info(request: &AllocRequest) -> AllocationCreateInfo
{
	let mut flags = AllocationCreateFlags::empty();

	if request.mapped {
		flags |= AllocationCreateFlags::MAPPED;
	}

	if request.host_sequential_write {
		flags |= AllocationCreateFlags::HOST_ACCESS_SEQUENTIAL_WRITE;
	}

	if request.dedicated {
		flags |= AllocationCreateFlags::DEDICATED_MEMORY;
	}

	let usage = match request.memory {
		Memory::Auto => MemoryUsage::Auto,
		Memory::AutoPreferDevice => MemoryUsage::AutoPreferDevice,
		Memory::GpuOnly => MemoryUsage::GpuOnly,
		Memory::CpuOnly => MemoryUsage::CpuOnly,
		Memory::CpuToGpu => MemoryUsage::CpuToGpu,
		Memory::GpuToCpu => MemoryUsage::GpuToCpu,
	};

	AllocationCreateInfo {
		flags,
		usage,
		priority: request.priority,
		..Default::default()
	}
}

pub struct Allocator
{
	inner: vk_mem::Allocator,
}

impl Allocator
{
	/// # Safety
	///
	/// The instance and device must outlive the allocator.
	pub unsafe fn new(instance: &Instance, device: &Device) -> VkResult<Self>
	{
		let info = AllocatorCreateInfo::new(instance.raw(), device.raw(), device.physical());

		// SAFETY: guaranteed by the caller.
		let inner = unsafe { vk_mem::Allocator::new(info) }?;

		Ok(Self { inner })
	}

	/// # Safety
	///
	/// `info` and everything it points to must be valid for the call.
	pub unsafe fn create_buffer(
		&self,
		info: &vk::BufferCreateInfo,
		request: &AllocRequest,
	) -> VkResult<(vk::Buffer, Allocation, *mut u8)>
	{
		// SAFETY: guaranteed by the caller.
		let (buffer, allocation) =
			unsafe { self.inner.create_buffer(info, &create_info(request)) }?;

		let mapped = if request.mapped {
			self.inner
				.get_allocation_info(&allocation)
				.mapped_data
				.cast()
		} else {
			std::ptr::null_mut()
		};

		Ok((buffer, allocation, mapped))
	}

	/// # Safety
	///
	/// `info` and everything it points to must be valid for the call.
	pub unsafe fn create_image(
		&self,
		info: &vk::ImageCreateInfo,
		request: &AllocRequest,
	) -> VkResult<(vk::Image, Allocation)>
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.inner.create_image(info, &create_info(request)) }
	}

	/// # Safety
	///
	/// The buffer and allocation must come from `create_buffer` and not be in use or already
	/// destroyed.
	pub unsafe fn destroy_buffer(&self, buffer: vk::Buffer, mut allocation: Allocation)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.inner.destroy_buffer(buffer, &mut allocation) };
	}

	/// # Safety
	///
	/// The image and allocation must come from `create_image` and not be in use or already
	/// destroyed.
	pub unsafe fn destroy_image(&self, image: vk::Image, mut allocation: Allocation)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.inner.destroy_image(image, &mut allocation) };
	}

	/// # Safety
	///
	/// The allocation must be live and host visible.
	pub unsafe fn map(&self, allocation: &mut Allocation) -> VkResult<*mut u8>
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.inner.map_memory(allocation) }
	}

	/// # Safety
	///
	/// The allocation must be live and currently mapped by `map`.
	pub unsafe fn unmap(&self, allocation: &mut Allocation)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.inner.unmap_memory(allocation) };
	}

	pub fn flush(&self, allocation: &Allocation, offset: u64, size: u64) -> VkResult<()>
	{
		self.inner.flush_allocation(allocation, offset, size)
	}

	pub fn invalidate(&self, allocation: &Allocation, offset: u64, size: u64) -> VkResult<()>
	{
		self.inner.invalidate_allocation(allocation, offset, size)
	}
}
