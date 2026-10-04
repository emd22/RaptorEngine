use std::collections::HashMap;
use std::sync::Mutex;

use ash::prelude::VkResult;
use ash::vk;

use crate::Device;

const FNV_INIT: u32 = 0x811c_9dc5;
const FNV_PRIME: u32 = 16_777_619;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DsLayoutEntry
{
	pub binding: u32,
	pub descriptor_type: vk::DescriptorType,
	pub stages: vk::ShaderStageFlags,
	pub count: u32,
}

fn fnv_bytes(mut hash: u32, bytes: &[u8]) -> u32
{
	for byte in bytes {
		hash ^= u32::from(*byte);
		hash = hash.wrapping_mul(FNV_PRIME);
	}

	hash
}

pub fn layout_id(entries: &[DsLayoutEntry]) -> u32
{
	let mut hash = FNV_INIT;

	for entry in entries {
		hash = fnv_bytes(hash, &entry.binding.to_ne_bytes());
		hash = fnv_bytes(hash, &entry.descriptor_type.as_raw().to_ne_bytes());
		hash = fnv_bytes(hash, &entry.stages.as_raw().to_ne_bytes());
	}

	hash
}

impl Device
{
	pub fn create_ds_layout(&self, entries: &[DsLayoutEntry]) -> VkResult<vk::DescriptorSetLayout>
	{
		let bindings: Vec<_> = entries
			.iter()
			.map(|entry| {
				vk::DescriptorSetLayoutBinding::default()
					.binding(entry.binding)
					.descriptor_type(entry.descriptor_type)
					.descriptor_count(entry.count)
					.stage_flags(entry.stages)
			})
			.collect();

		let info = vk::DescriptorSetLayoutCreateInfo::default().bindings(&bindings);

		// SAFETY: the create info and the bindings outlive the call.
		unsafe { self.raw().create_descriptor_set_layout(&info, None) }
	}

	/// # Safety
	///
	/// The layout must belong to this device and not be in use.
	pub unsafe fn destroy_ds_layout(&self, layout: vk::DescriptorSetLayout)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().destroy_descriptor_set_layout(layout, None) };
	}
}

#[derive(Default)]
pub struct DsLayoutCache
{
	layouts: Mutex<HashMap<u32, vk::DescriptorSetLayout>>,
}

impl DsLayoutCache
{
	pub fn request(
		&self,
		device: &Device,
		entries: &[DsLayoutEntry],
	) -> VkResult<(u32, vk::DescriptorSetLayout)>
	{
		let id = layout_id(entries);

		let mut layouts = self
			.layouts
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner());

		if let Some(layout) = layouts.get(&id) {
			return Ok((id, *layout));
		}

		let layout = device.create_ds_layout(entries)?;
		layouts.insert(id, layout);

		Ok((id, layout))
	}

	pub fn get(&self, id: u32) -> Option<vk::DescriptorSetLayout>
	{
		self.layouts
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.get(&id)
			.copied()
	}

	/// # Safety
	///
	/// The layout must not be in use by any pipeline layout creation or descriptor set allocation
	/// in flight.
	pub unsafe fn free(&self, device: &Device, id: u32)
	{
		let layout = self
			.layouts
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.remove(&id);

		if let Some(layout) = layout {
			// SAFETY: guaranteed by the caller.
			unsafe { device.destroy_ds_layout(layout) };
		}
	}

	/// # Safety
	///
	/// As for `free`, for every cached layout.
	pub unsafe fn destroy(&self, device: &Device)
	{
		let mut layouts = self
			.layouts
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner());

		for (_, layout) in layouts.drain() {
			// SAFETY: guaranteed by the caller.
			unsafe { device.destroy_ds_layout(layout) };
		}
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn entry(
		binding: u32,
		descriptor_type: vk::DescriptorType,
		stages: vk::ShaderStageFlags,
	) -> DsLayoutEntry
	{
		DsLayoutEntry {
			binding,
			descriptor_type,
			stages,
			count: 1,
		}
	}

	#[test]
	fn empty_layouts_hash_to_the_fnv_offset()
	{
		assert_eq!(layout_id(&[]), FNV_INIT);
	}

	#[test]
	fn fnv_matches_the_reference_vectors()
	{
		assert_eq!(fnv_bytes(FNV_INIT, b"a"), 0xe40c_292c);
		assert_eq!(fnv_bytes(FNV_INIT, b"foobar"), 0xbf9c_f968);
	}

	#[test]
	fn ids_depend_on_every_field_and_the_order()
	{
		let a = entry(
			0,
			vk::DescriptorType::UNIFORM_BUFFER_DYNAMIC,
			vk::ShaderStageFlags::VERTEX,
		);
		let b = entry(
			1,
			vk::DescriptorType::COMBINED_IMAGE_SAMPLER,
			vk::ShaderStageFlags::FRAGMENT,
		);

		assert_eq!(layout_id(&[a, b]), layout_id(&[a, b]));
		assert_ne!(layout_id(&[a, b]), layout_id(&[b, a]));
		assert_ne!(
			layout_id(&[a]),
			layout_id(&[DsLayoutEntry { binding: 2, ..a }])
		);
		assert_ne!(
			layout_id(&[a]),
			layout_id(&[DsLayoutEntry {
				stages: vk::ShaderStageFlags::FRAGMENT,
				..a
			}])
		);
		assert_ne!(
			layout_id(&[a]),
			layout_id(&[DsLayoutEntry {
				descriptor_type: vk::DescriptorType::STORAGE_BUFFER_DYNAMIC,
				..a
			}])
		);
	}

	#[test]
	fn the_count_does_not_change_the_id()
	{
		let a = entry(
			0,
			vk::DescriptorType::COMBINED_IMAGE_SAMPLER,
			vk::ShaderStageFlags::FRAGMENT,
		);

		assert_eq!(
			layout_id(&[a]),
			layout_id(&[DsLayoutEntry { count: 4, ..a }])
		);
	}
}
