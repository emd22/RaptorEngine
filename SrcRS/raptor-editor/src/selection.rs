use crate::host::{EditorHost, MaterialId, ObjectId, TAG_BLOCKOUT};

pub const MAX_SELECTED: usize = 64;

#[derive(Clone, Copy, Debug)]
struct Entry {
	object: ObjectId,
	stored_material: MaterialId,
}

#[derive(Default, Debug)]
pub struct Selection {
	entries: Vec<Entry>,
}

fn is_blockout(host: &dyn EditorHost, object: ObjectId) -> bool {
	host.object_tags(object) & TAG_BLOCKOUT != 0
}

fn show_as_selected(host: &mut dyn EditorHost, object: ObjectId) {
	if is_blockout(host, object) {
		let material = host.selection_material();
		host.set_object_material(object, material);
	}
}

fn show_as_deselected(host: &mut dyn EditorHost, object: ObjectId, material: MaterialId) {
	if host.object_exists(object) && is_blockout(host, object) {
		host.set_object_material(object, material);
	}
}

impl Selection {
	pub fn new() -> Self {
		Self::default()
	}

	pub fn add(&mut self, host: &mut dyn EditorHost, object: ObjectId) -> bool {
		if self.contains(object) {
			return true;
		}

		if self.entries.len() >= MAX_SELECTED {
			return false;
		}

		self.entries.push(Entry {
			object,
			stored_material: host.object_material(object),
		});

		show_as_selected(host, object);

		true
	}

	pub fn remove(&mut self, host: &mut dyn EditorHost, object: ObjectId) {
		if let Some(index) = self.entries.iter().position(|entry| entry.object == object) {
			let entry = self.entries.remove(index);

			show_as_deselected(host, entry.object, entry.stored_material);
		}
	}

	pub fn clear(&mut self, host: &mut dyn EditorHost) {
		for entry in std::mem::take(&mut self.entries) {
			show_as_deselected(host, entry.object, entry.stored_material);
		}
	}

	pub fn forget(&mut self) {
		self.entries.clear();
	}

	pub fn prune(&mut self, host: &dyn EditorHost) -> bool {
		let before = self.entries.len();

		self.entries
			.retain(|entry| host.object_exists(entry.object));

		self.entries.len() != before
	}

	pub fn contains(&self, object: ObjectId) -> bool {
		self.entries.iter().any(|entry| entry.object == object)
	}

	pub fn is_empty(&self) -> bool {
		self.entries.is_empty()
	}

	pub fn len(&self) -> usize {
		self.entries.len()
	}

	pub fn get(&self, index: usize) -> Option<ObjectId> {
		self.entries.get(index).map(|entry| entry.object)
	}

	pub fn objects(&self) -> impl Iterator<Item = ObjectId> + '_ {
		self.entries.iter().map(|entry| entry.object)
	}

	pub fn last(&self) -> Option<ObjectId> {
		self.entries.last().map(|entry| entry.object)
	}

	pub fn stored_material(&self, host: &dyn EditorHost, object: ObjectId) -> MaterialId {
		self.entries
			.iter()
			.find(|entry| entry.object == object)
			.map_or_else(
				|| host.object_material(object),
				|entry| entry.stored_material,
			)
	}

	pub fn set_stored_material(
		&mut self,
		host: &mut dyn EditorHost,
		object: ObjectId,
		material: MaterialId,
	) {
		if !is_blockout(host, object) {
			return;
		}

		if let Some(entry) = self.entries.iter_mut().find(|entry| entry.object == object) {
			entry.stored_material = material;
		}

		host.set_object_material(object, material);
	}
}
