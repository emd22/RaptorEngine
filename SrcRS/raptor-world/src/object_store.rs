use std::sync::{Mutex, MutexGuard};

use raptor_entity::{EntityCore, ObjectCore};

use crate::slots::{NOT_FOUND, SlotSet};

/// What an object is, owned here so that the engine can reach every object by id.
pub struct ObjectRecord
{
	pub entity: Box<EntityCore>,
	pub object: Box<ObjectCore>,
	pub name_hash: u32,
}

struct Inner
{
	slots: SlotSet,
	records: Vec<Option<ObjectRecord>>,
}

/// The objects in the world, in a fixed number of slots. An object's id is its slot, and the
/// records never move, so a pointer to one stays good until the object is freed.
pub struct ObjectStore
{
	inner: Mutex<Inner>,
	capacity: u32,
}

/// Pointers to the records of an object, which last until it is freed.
#[derive(Clone, Copy, Debug)]
pub struct ObjectHandles
{
	pub id: u32,
	pub entity: *mut EntityCore,
	pub object: *mut ObjectCore,
}

impl ObjectStore
{
	pub fn new(capacity: u32) -> Self
	{
		Self {
			inner: Mutex::new(Inner {
				slots: SlotSet::new(capacity, false),
				records: (0..capacity).map(|_| None).collect(),
			}),
			capacity,
		}
	}

	fn lock(&self) -> MutexGuard<'_, Inner>
	{
		self.inner
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
	}

	pub fn capacity(&self) -> u32
	{
		self.capacity
	}

	fn handles(record: &mut ObjectRecord, id: u32) -> ObjectHandles
	{
		ObjectHandles {
			id,
			entity: &raw mut *record.entity,
			object: &raw mut *record.object,
		}
	}

	/// Takes the lowest free slot for a new object, or `None` if every slot is taken.
	pub fn alloc(&self, name_hash: u32) -> Option<ObjectHandles>
	{
		let mut inner = self.lock();

		let id = inner.slots.find_next_free(0);

		if id == NOT_FOUND || id >= self.capacity {
			return None;
		}

		inner.slots.set(id);

		let mut entity = Box::<EntityCore>::default();
		entity.id = id;

		let mut record = ObjectRecord {
			entity,
			object: Box::default(),
			name_hash,
		};

		let handles = Self::handles(&mut record, id);

		inner.records[id as usize] = Some(record);

		Some(handles)
	}

	pub fn get(&self, id: u32) -> Option<ObjectHandles>
	{
		let mut inner = self.lock();

		inner
			.records
			.get_mut(id as usize)?
			.as_mut()
			.map(|record| Self::handles(record, id))
	}

	pub fn is_used(&self, id: u32) -> bool
	{
		id < self.capacity && self.lock().slots.get(id)
	}

	/// Frees the slot and the records in it.
	pub fn free(&self, id: u32)
	{
		let mut inner = self.lock();

		if let Some(record) = inner.records.get_mut(id as usize) {
			*record = None;
			inner.slots.unset(id);
		}
	}

	pub fn set_name_hash(&self, id: u32, hash: u32)
	{
		if let Some(Some(record)) = self.lock().records.get_mut(id as usize) {
			record.name_hash = hash;
		}
	}

	pub fn find_by_name_hash(&self, hash: u32) -> Option<u32>
	{
		self.lock()
			.records
			.iter()
			.position(|record| {
				record
					.as_ref()
					.is_some_and(|record| record.name_hash == hash)
			})
			.map(|index| index as u32)
	}

	/// The ids of the objects that are in use, lowest first.
	pub fn used_ids(&self) -> Vec<u32>
	{
		let inner = self.lock();

		(0..self.capacity)
			.filter(|id| inner.slots.get(*id))
			.collect()
	}

	/// The ids of the objects that have any of `tags`.
	pub fn ids_with_tags(&self, tags: u32) -> Vec<u32>
	{
		let inner = self.lock();

		inner
			.records
			.iter()
			.enumerate()
			.filter(|(_, record)| {
				record
					.as_ref()
					.is_some_and(|record| record.object.has_tag(tags))
			})
			.map(|(id, _)| id as u32)
			.collect()
	}

	pub fn len(&self) -> u32
	{
		self.lock()
			.records
			.iter()
			.filter(|record| record.is_some())
			.count() as u32
	}

	pub fn is_empty(&self) -> bool
	{
		self.len() == 0
	}

	pub fn clear(&self)
	{
		let mut inner = self.lock();

		inner.slots.clear_all();
		inner.records.iter_mut().for_each(|record| *record = None);
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn objects_take_the_lowest_free_slot_and_know_their_id()
	{
		let store = ObjectStore::new(8);

		let a = store.alloc(10).unwrap();
		let b = store.alloc(20).unwrap();

		assert_eq!((a.id, b.id), (0, 1));

		// SAFETY: the records are live.
		assert_eq!(unsafe { (*b.entity).id }, 1);

		store.free(0);

		assert_eq!(store.alloc(30).unwrap().id, 0);
	}

	#[test]
	fn a_full_store_gives_nothing()
	{
		let store = ObjectStore::new(2);

		store.alloc(0).unwrap();
		store.alloc(0).unwrap();

		assert!(store.alloc(0).is_none());
	}

	#[test]
	fn names_and_tags_find_objects()
	{
		let store = ObjectStore::new(8);

		let a = store.alloc(7).unwrap();
		store.alloc(9).unwrap();

		// SAFETY: the record is live.
		unsafe { (*a.object).set_tag(0b10, true) };

		assert_eq!(store.find_by_name_hash(9), Some(1));
		assert_eq!(store.find_by_name_hash(5), None);
		assert_eq!(store.ids_with_tags(0b10), vec![0]);
		assert_eq!(store.used_ids(), vec![0, 1]);
		assert_eq!(store.len(), 2);
	}

	#[test]
	fn the_records_stay_where_they_are_as_others_come_and_go()
	{
		let store = ObjectStore::new(8);

		let first = store.alloc(1).unwrap();

		for _ in 0..4 {
			let extra = store.alloc(2).unwrap();
			store.free(extra.id);
		}

		assert_eq!(store.get(0).unwrap().entity, first.entity);
	}
}
