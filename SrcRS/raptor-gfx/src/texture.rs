use std::sync::{Arc, Mutex};

use crate::core::GpuCore;
use crate::image::Image;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct TextureId(u32);

impl TextureId {
	pub const NULL: TextureId = TextureId(u32::MAX);

	pub fn from_index(index: u32) -> Self {
		Self(index)
	}

	pub fn index(self) -> u32 {
		self.0
	}

	pub fn is_null(self) -> bool {
		self == Self::NULL
	}
}

impl Default for TextureId {
	fn default() -> Self {
		Self::NULL
	}
}

impl std::fmt::Display for TextureId {
	fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
		write!(f, "TextureID({})", self.0)
	}
}

pub struct Textures {
	core: Arc<GpuCore>,
	slots: Mutex<Vec<Option<Image>>>,
}

impl Textures {
	pub const MAX_TEXTURES: usize = 512;

	pub fn new(core: &Arc<GpuCore>) -> Self {
		Self {
			core: core.clone(),
			slots: Mutex::new(Vec::new()),
		}
	}

	fn lock(&self) -> std::sync::MutexGuard<'_, Vec<Option<Image>>> {
		self.slots
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
	}

	pub fn new_texture(&self) -> (TextureId, Image) {
		let mut slots = self.lock();
		let image = Image::new(&self.core);

		let index = match slots.iter().position(Option::is_none) {
			Some(index) => {
				slots[index] = Some(image.clone());
				index
			}
			None => {
				assert!(slots.len() < Self::MAX_TEXTURES, "ran out of texture slots");
				slots.push(Some(image.clone()));
				slots.len() - 1
			}
		};

		(TextureId(index as u32), image)
	}

	pub fn get(&self, id: TextureId) -> Option<Image> {
		if id.is_null() {
			return None;
		}

		self.lock().get(id.0 as usize).and_then(Clone::clone)
	}

	pub fn destroy(&self, id: TextureId) {
		if id.is_null() {
			return;
		}

		let image = self.lock().get_mut(id.0 as usize).and_then(Option::take);

		drop(image);
	}

	pub fn release_all(&self) {
		let images: Vec<_> = self.lock().drain(..).flatten().collect();

		drop(images);
	}

	pub fn live_count(&self) -> usize {
		self.lock().iter().flatten().count()
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn the_null_id_is_null_and_others_are_not() {
		assert!(TextureId::NULL.is_null());
		assert!(!TextureId::from_index(0).is_null());
		assert_eq!(TextureId::default(), TextureId::NULL);
	}
}
