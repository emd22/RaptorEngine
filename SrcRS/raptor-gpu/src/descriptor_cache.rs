use std::collections::HashMap;
use std::sync::Arc;

use ash::prelude::VkResult;
use ash::vk::{self, Handle};

use crate::descriptor_store::{DescriptorIdEntry, KIND_BUFFER, KIND_IMAGE, descriptor_id};
use crate::{
	Allocator, BufferRecord, BufferType, DescriptorPoolRecord, DescriptorWrite, Device,
	DsLayoutCache, DsLayoutEntry, ImageRecord, Level,
};

const DEFAULT_POOL_SETS: u32 = 128;

/// What a set binds. Images and buffers are held by their records rather than by handle, so a set
/// follows an image that is recreated for a new window size, or a buffer that is replaced.
pub enum DescriptorResource
{
	Image
	{
		image: Arc<ImageRecord>,
		sampler: vk::Sampler,
	},
	Buffer
	{
		buffer: Arc<BufferRecord>,
		offset: u64,
		range: u64,
	},
}

pub struct DescriptorEntry
{
	pub binding: u32,
	pub stages: vk::ShaderStageFlags,
	pub resource: DescriptorResource,
}

/// The records a request refers to, which are not retained until the set is made.
#[derive(Clone, Copy)]
pub enum DescriptorResourceRef
{
	Image
	{
		image: *const ImageRecord,
		sampler: vk::Sampler,
	},
	Buffer
	{
		buffer: *const BufferRecord,
		offset: u64,
		range: u64,
	},
}

#[derive(Clone, Copy)]
pub struct DescriptorEntryRef
{
	pub binding: u32,
	pub stages: vk::ShaderStageFlags,
	pub resource: DescriptorResourceRef,
}

impl DescriptorEntryRef
{
	/// # Safety
	///
	/// The record must be live.
	unsafe fn layout_entry(&self) -> DsLayoutEntry
	{
		let descriptor_type = match self.resource {
			DescriptorResourceRef::Image { .. } => vk::DescriptorType::COMBINED_IMAGE_SAMPLER,
			DescriptorResourceRef::Buffer { buffer, .. } => {
				// SAFETY: guaranteed by the caller.
				let raw_type = unsafe { &*buffer }.fields().buffer_type;

				BufferType::from_raw(raw_type)
					.unwrap_or(BufferType::Storage)
					.descriptor_type()
			}
		};

		DsLayoutEntry {
			binding: self.binding,
			descriptor_type,
			stages: self.stages,
			count: 1,
		}
	}

	/// # Safety
	///
	/// The record must be live.
	unsafe fn id_entry(&self) -> DescriptorIdEntry
	{
		// SAFETY: guaranteed by the caller.
		let (kind, handle) = unsafe {
			match self.resource {
				DescriptorResourceRef::Image { image, .. } => (KIND_IMAGE, (*image).fields().image),
				DescriptorResourceRef::Buffer { buffer, .. } => {
					(KIND_BUFFER, (*buffer).fields().buffer)
				}
			}
		};

		DescriptorIdEntry {
			binding: self.binding,
			kind,
			handle,
		}
	}

	/// Takes a reference to the record, which has to be given back through `DescriptorSetRecord`.
	///
	/// # Safety
	///
	/// The record must come from `into_raw` and be live.
	unsafe fn retain(&self) -> DescriptorEntry
	{
		let resource = match self.resource {
			DescriptorResourceRef::Image { image, sampler } => {
				// SAFETY: guaranteed by the caller.
				unsafe { ImageRecord::retain(image) };

				DescriptorResource::Image {
					// SAFETY: a reference was just taken for this `Arc`.
					image: unsafe { Arc::from_raw(image) },
					sampler,
				}
			}
			DescriptorResourceRef::Buffer {
				buffer,
				offset,
				range,
			} => {
				// SAFETY: guaranteed by the caller.
				unsafe { BufferRecord::retain(buffer) };

				DescriptorResource::Buffer {
					// SAFETY: a reference was just taken for this `Arc`.
					buffer: unsafe { Arc::from_raw(buffer) },
					offset,
					range,
				}
			}
		};

		DescriptorEntry {
			binding: self.binding,
			stages: self.stages,
			resource,
		}
	}
}

impl DescriptorEntry
{
	/// The write for the resource as it is now, or none if it has gone.
	fn write(&self) -> Option<DescriptorWrite>
	{
		match &self.resource {
			DescriptorResource::Image { image, sampler } => {
				let fields = image.fields();

				(fields.view != 0).then(|| DescriptorWrite::Image {
					binding: self.binding,
					sampler: *sampler,
					view: vk::ImageView::from_raw(fields.view),
				})
			}
			DescriptorResource::Buffer {
				buffer,
				offset,
				range,
			} => {
				let fields = buffer.fields();

				(fields.buffer != 0).then(|| DescriptorWrite::Buffer {
					binding: self.binding,
					buffer: vk::Buffer::from_raw(fields.buffer),
					offset: *offset,
					range: *range,
					buffer_type: BufferType::from_raw(fields.buffer_type)
						.unwrap_or(BufferType::Storage),
				})
			}
		}
	}

	fn is_buffer(&self) -> bool
	{
		matches!(self.resource, DescriptorResource::Buffer { .. })
	}
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct DescriptorSetFields
{
	pub set: u64,
	pub id: u32,
	pub layout_id: u32,
	pub buffer_count: u32,
	pub has_dynamic_offsets: u8,
	pub built: u8,
}

#[repr(C)]
pub struct DescriptorSetRecord
{
	pub fields: DescriptorSetFields,
	entries: Vec<DescriptorEntry>,
}

impl DescriptorSetRecord
{
	/// Writes the entries into the set, using the handles the resources have now. Resources that
	/// are gone are left out.
	fn build(&mut self, device: &Device)
	{
		if self.entries.is_empty() {
			device.log().log(
				Level::Warning,
				&format!("Building empty descriptor set {:x}", self.fields.set),
			);
			return;
		}

		let mut writes = Vec::with_capacity(self.entries.len());

		for entry in &self.entries {
			match entry.write() {
				Some(write) => writes.push(write),
				None => device.log().log(
					Level::Warning,
					&format!(
						"Binding {} of descriptor set {:x} refers to a resource that is gone",
						entry.binding, self.fields.set
					),
				),
			}
		}

		if !writes.is_empty() {
			// SAFETY: the set is not in use, and every handle in the writes was just read from a
			// live record.
			unsafe {
				device.update_descriptor_set(vk::DescriptorSet::from_raw(self.fields.set), &writes)
			};
		}

		self.fields.built = 1;
	}

	/// Makes the set again in `pool` and rewrites it, for when the images or buffers behind it were
	/// replaced.
	///
	/// # Safety
	///
	/// The device must be the one the set was made with, `pool` the pool it came from, and the old
	/// set must not be in use.
	unsafe fn rebuild(
		&mut self,
		device: &Device,
		pool: &mut DescriptorPoolRecord,
		layout: vk::DescriptorSetLayout,
	) -> VkResult<()>
	{
		if self.entries.is_empty() {
			return Ok(());
		}

		if self.fields.set != 0 {
			// SAFETY: guaranteed by the caller.
			unsafe { pool.free_set(device, vk::DescriptorSet::from_raw(self.fields.set)) };
			self.fields.set = 0;
		}

		self.fields.set = pool.allocate_set(device, layout)?.as_raw();
		self.fields.built = 0;

		self.build(device);

		Ok(())
	}

	/// # Safety
	///
	/// `cmd` must be recording, `layout` the bound pipeline's layout, and the offsets one per
	/// buffer in the set.
	pub unsafe fn bind(
		&self,
		device: &Device,
		cmd: vk::CommandBuffer,
		bind_point: vk::PipelineBindPoint,
		layout: vk::PipelineLayout,
		first_set: u32,
		offsets: &[u32],
	)
	{
		// SAFETY: guaranteed by the caller.
		unsafe {
			device.cmd_bind_descriptor_sets(
				cmd,
				bind_point,
				layout,
				first_set,
				&[vk::DescriptorSet::from_raw(self.fields.set)],
				offsets,
			)
		};
	}

	/// Gives the references the set held back, destroying whatever they were the last owners of.
	///
	/// # Safety
	///
	/// The device and allocator must be the ones the resources were made with, and nothing may be
	/// using them.
	unsafe fn release_entries(&mut self, device: &Device, allocator: &Allocator)
	{
		for entry in self.entries.drain(..) {
			match entry.resource {
				DescriptorResource::Image { image, .. } => {
					// SAFETY: guaranteed by the caller.
					unsafe { ImageRecord::release_arc(image, device, allocator) };
				}
				DescriptorResource::Buffer { buffer, .. } => {
					// SAFETY: guaranteed by the caller.
					unsafe { BufferRecord::release_arc(buffer, allocator) };
				}
			}
		}
	}
}

/// Makes and keeps the descriptor sets, one for each distinct combination of resources.
#[derive(Default)]
pub struct DescriptorCache
{
	pools: Vec<DescriptorPoolRecord>,
	sets: HashMap<u32, Box<DescriptorSetRecord>>,
}

impl DescriptorCache
{
	fn pool(&mut self, device: &Device) -> VkResult<&mut DescriptorPoolRecord>
	{
		if self.pools.is_empty() {
			let sizes = vec![
				vk::DescriptorPoolSize {
					ty: vk::DescriptorType::COMBINED_IMAGE_SAMPLER,
					descriptor_count: 128,
				},
				vk::DescriptorPoolSize {
					ty: vk::DescriptorType::STORAGE_BUFFER_DYNAMIC,
					descriptor_count: 64,
				},
				vk::DescriptorPoolSize {
					ty: vk::DescriptorType::UNIFORM_BUFFER_DYNAMIC,
					descriptor_count: 64,
				},
			];

			self.pools.push(DescriptorPoolRecord::create(
				device,
				sizes,
				DEFAULT_POOL_SETS,
				true,
			)?);
		}

		Ok(&mut self.pools[0])
	}

	/// Finds the set for `entries`, making it if there is none. The returned set stays at the same
	/// address until it is freed.
	///
	/// # Safety
	///
	/// The device must be live, the layout cache the one the layouts are looked up in, and every
	/// record in `entries` must come from its `into_raw` and be live.
	pub unsafe fn request(
		&mut self,
		device: &Device,
		layouts: &DsLayoutCache,
		entries: &[DescriptorEntryRef],
	) -> VkResult<(u32, *mut DescriptorSetRecord)>
	{
		// SAFETY: guaranteed by the caller.
		let layout_entries: Vec<_> = entries
			.iter()
			.map(|entry| unsafe { entry.layout_entry() })
			.collect();

		let (layout_id, layout) = layouts.request(device, &layout_entries)?;

		// SAFETY: guaranteed by the caller.
		let id_entries: Vec<_> = entries
			.iter()
			.map(|entry| unsafe { entry.id_entry() })
			.collect();
		let id = descriptor_id(&id_entries);

		if let Some(set) = self.sets.get_mut(&id) {
			return Ok((id, &raw mut **set));
		}

		let set = self.pool(device)?.allocate_set(device, layout)?;

		let has_buffer = entries
			.iter()
			.any(|entry| matches!(entry.resource, DescriptorResourceRef::Buffer { .. }));

		let mut record = Box::new(DescriptorSetRecord {
			fields: DescriptorSetFields {
				set: set.as_raw(),
				id,
				layout_id,
				buffer_count: 0,
				has_dynamic_offsets: u8::from(has_buffer),
				built: 0,
			},
			// SAFETY: guaranteed by the caller.
			entries: entries
				.iter()
				.map(|entry| unsafe { entry.retain() })
				.collect(),
		});

		record.fields.buffer_count = record
			.entries
			.iter()
			.filter(|entry| entry.is_buffer())
			.count() as u32;
		record.build(device);

		let pointer = &raw mut *record;

		self.sets.insert(id, record);

		Ok((id, pointer))
	}

	pub fn find(&mut self, id: u32) -> Option<*mut DescriptorSetRecord>
	{
		self.sets.get_mut(&id).map(|set| &raw mut **set)
	}

	/// Frees a set and the references it held. The layout stays in the layout cache, as other sets
	/// are likely to share it.
	///
	/// # Safety
	///
	/// The device and allocator must be the ones the set was made with, and the set must not be in
	/// use.
	pub unsafe fn free(&mut self, device: &Device, allocator: &Allocator, id: u32)
	{
		let Some(mut set) = self.sets.remove(&id) else {
			return;
		};

		if let Some(pool) = self.pools.first() {
			// SAFETY: guaranteed by the caller.
			unsafe { pool.free_set(device, vk::DescriptorSet::from_raw(set.fields.set)) };
		}

		// SAFETY: guaranteed by the caller.
		unsafe { set.release_entries(device, allocator) };
	}

	/// Rewrites every set with the current handles of its resources, in a fresh set from the pool.
	///
	/// # Safety
	///
	/// The device must be live and no set may be in use.
	pub unsafe fn rebuild_all(&mut self, device: &Device, layouts: &DsLayoutCache) -> VkResult<()>
	{
		self.pool(device)?;

		let Self { pools, sets } = self;

		for set in sets.values_mut() {
			let Some(layout) = layouts.get(set.fields.layout_id) else {
				device.log().log(
					Level::Error,
					&format!(
						"DescriptorSet::Rebuild: Layout {} does not refer to an existing descriptor set layout.",
						set.fields.layout_id
					),
				);
				continue;
			};

			// SAFETY: guaranteed by the caller.
			unsafe { set.rebuild(device, &mut pools[0], layout) }?;
		}

		Ok(())
	}

	/// # Safety
	///
	/// The device and allocator must be the ones everything was made with, and nothing may be using
	/// it.
	pub unsafe fn destroy(mut self, device: &Device, allocator: &Allocator)
	{
		for (_, mut set) in self.sets.drain() {
			// SAFETY: guaranteed by the caller.
			unsafe { set.release_entries(device, allocator) };
		}

		for pool in self.pools.drain(..) {
			// SAFETY: guaranteed by the caller.
			unsafe { pool.destroy(device) };
		}
	}
}

#[cfg(test)]
mod tests
{
	use std::mem::{offset_of, size_of};

	use super::*;
	use crate::ImageFormat;

	#[test]
	fn the_public_fields_have_the_layout_the_c_header_declares()
	{
		assert_eq!(offset_of!(DescriptorSetRecord, fields), 0);
		assert_eq!(size_of::<DescriptorSetFields>(), 24);
		assert_eq!(offset_of!(DescriptorSetFields, id), 8);
		assert_eq!(offset_of!(DescriptorSetFields, layout_id), 12);
		assert_eq!(offset_of!(DescriptorSetFields, buffer_count), 16);
		assert_eq!(offset_of!(DescriptorSetFields, has_dynamic_offsets), 20);
		assert_eq!(offset_of!(DescriptorSetFields, built), 21);
	}

	#[test]
	fn a_write_follows_the_image_the_record_holds_now()
	{
		let image = ImageRecord::new();

		let entry = DescriptorEntry {
			binding: 2,
			stages: vk::ShaderStageFlags::FRAGMENT,
			resource: DescriptorResource::Image {
				image: image.clone(),
				sampler: vk::Sampler::from_raw(9),
			},
		};

		assert!(entry.write().is_none());

		// SAFETY: nothing else uses the record.
		unsafe { image.wrap_external(vk::Image::from_raw(5), (4, 4), ImageFormat::Rgba8UNorm) };
		assert!(entry.write().is_none());

		let raw = ImageRecord::into_raw(image.clone());
		// SAFETY: the pointer comes from `into_raw`.
		unsafe { ImageRecord::release(raw, None, None) };
	}

	#[test]
	fn a_write_for_an_empty_buffer_slot_is_left_out()
	{
		let entry = DescriptorEntry {
			binding: 0,
			stages: vk::ShaderStageFlags::VERTEX,
			resource: DescriptorResource::Buffer {
				buffer: BufferRecord::new(),
				offset: 0,
				range: 16,
			},
		};

		assert!(entry.write().is_none());
		assert!(entry.is_buffer());
	}
}
