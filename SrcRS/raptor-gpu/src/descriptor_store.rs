use ash::prelude::VkResult;
use ash::vk::{self, Handle};

use crate::Device;
use crate::ds_layout::fnv_bytes;

const FNV32_INIT: u32 = 0x811c_9dc5;

pub const KIND_IMAGE: u32 = 1;
pub const KIND_BUFFER: u32 = 2;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct DescriptorPoolFields
{
	pub pool: u64,
	pub set_capacity: u32,
	pub sets_used: u32,
}

#[repr(C)]
pub struct DescriptorPoolRecord
{
	pub fields: DescriptorPoolFields,
	sizes: Vec<vk::DescriptorPoolSize>,
	free_sets: bool,
}

impl DescriptorPoolRecord
{
	pub fn create(
		device: &Device,
		sizes: Vec<vk::DescriptorPoolSize>,
		max_sets: u32,
		free_sets: bool,
	) -> VkResult<Box<Self>>
	{
		let pool = device.create_descriptor_pool(&sizes, max_sets, free_sets)?;

		Ok(Box::new(Self {
			fields: DescriptorPoolFields {
				pool: pool.as_raw(),
				set_capacity: max_sets,
				sets_used: 0,
			},
			sizes,
			free_sets,
		}))
	}

	/// # Safety
	///
	/// The device must be the one the pool was created with, and none of its sets may be in use.
	/// Every set allocated from the old pool is invalid afterwards.
	pub unsafe fn recreate(&mut self, device: &Device) -> VkResult<()>
	{
		// SAFETY: guaranteed by the caller.
		unsafe { device.destroy_descriptor_pool(vk::DescriptorPool::from_raw(self.fields.pool)) };
		self.fields.pool = 0;

		let pool =
			device.create_descriptor_pool(&self.sizes, self.fields.set_capacity, self.free_sets)?;

		self.fields.pool = pool.as_raw();
		self.fields.sets_used = 0;

		Ok(())
	}

	pub fn allocate_set(
		&mut self,
		device: &Device,
		layout: vk::DescriptorSetLayout,
	) -> VkResult<vk::DescriptorSet>
	{
		self.fields.sets_used += 1;

		device.allocate_descriptor_set(vk::DescriptorPool::from_raw(self.fields.pool), layout)
	}

	/// # Safety
	///
	/// The pool must have been created with free sets, and the set must come from it and not be in
	/// use.
	pub unsafe fn free_set(&self, device: &Device, set: vk::DescriptorSet)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { device.free_descriptor_set(vk::DescriptorPool::from_raw(self.fields.pool), set) };
	}

	/// # Safety
	///
	/// The device must be the one the pool was created with, and none of its sets may be in use.
	pub unsafe fn destroy(self, device: &Device)
	{
		if self.fields.pool != 0 {
			// SAFETY: guaranteed by the caller.
			unsafe {
				device.destroy_descriptor_pool(vk::DescriptorPool::from_raw(self.fields.pool))
			};
		}
	}
}

#[derive(Clone, Copy, Debug)]
pub struct DescriptorIdEntry
{
	pub binding: u32,
	pub kind: u32,
	pub handle: u64,
}

pub fn descriptor_id(entries: &[DescriptorIdEntry]) -> u32
{
	let mut hash = FNV32_INIT;

	for entry in entries {
		if entry.binding != 0 {
			hash = fnv_bytes(hash, &entry.binding.to_ne_bytes());
		}

		if entry.kind == KIND_IMAGE || entry.kind == KIND_BUFFER {
			hash = fnv_bytes(hash, &entry.handle.to_ne_bytes());
		}
	}

	hash
}

#[cfg(test)]
mod tests
{
	use std::mem::{offset_of, size_of};

	use super::*;

	fn entry(binding: u32, kind: u32, handle: u64) -> DescriptorIdEntry
	{
		DescriptorIdEntry {
			binding,
			kind,
			handle,
		}
	}

	#[test]
	fn ids_match_the_cpp_hash_of_the_same_entries()
	{
		assert_eq!(descriptor_id(&[]), 0x811c_9dc5);
		assert_eq!(descriptor_id(&[entry(0, 1, 0x1234)]), 0x8548_e0cb);
		assert_eq!(
			descriptor_id(&[
				entry(0, 2, 0xdead_beef),
				entry(1, 1, 0x8500_0000_0085),
				entry(7, 2, 5)
			]),
			0x5e11_48ba
		);
		assert_eq!(descriptor_id(&[entry(3, 0, 99)]), 0x9bc2_3426);
	}

	#[test]
	fn the_public_fields_have_the_layout_the_c_header_declares()
	{
		assert_eq!(offset_of!(DescriptorPoolRecord, fields), 0);
		assert_eq!(size_of::<DescriptorPoolFields>(), 16);
		assert_eq!(offset_of!(DescriptorPoolFields, set_capacity), 8);
		assert_eq!(offset_of!(DescriptorPoolFields, sets_used), 12);
	}
}
