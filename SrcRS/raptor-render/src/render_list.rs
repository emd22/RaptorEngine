use crate::names::PipelineHandle;

pub const INVALID_OBJECT: u32 = 1 << 31;

#[derive(Default)]
pub struct RenderList {
	sections: Vec<Vec<u32>>,
}

impl RenderList {
	pub fn add(&mut self, pipeline: PipelineHandle, object: u32) {
		self.section_mut(pipeline).push(object);
	}

	pub fn invalidate(&mut self, object: u32) {
		for section in &mut self.sections {
			if let Some(found) = section
				.iter_mut()
				.find(|found| **found & !INVALID_OBJECT == object & !INVALID_OBJECT)
			{
				*found |= INVALID_OBJECT;
			}
		}
	}

	pub fn clear_section(&mut self, pipeline: PipelineHandle) {
		self.section_mut(pipeline).clear();
	}

	pub fn clear(&mut self) {
		for section in &mut self.sections {
			section.clear();
		}
	}

	pub fn count(&self) -> usize {
		self.sections.iter().map(Vec::len).sum()
	}

	pub fn duplicates(&self, object: u32) -> usize {
		self.sections
			.iter()
			.flatten()
			.filter(|found| **found == object)
			.count()
	}

	pub fn section(&self, pipeline: PipelineHandle) -> &[u32] {
		self.sections
			.get(pipeline.0 as usize)
			.map_or(&[], Vec::as_slice)
	}

	pub fn section_mut(&mut self, pipeline: PipelineHandle) -> &mut Vec<u32> {
		let index = pipeline.0 as usize;

		if index >= self.sections.len() {
			self.sections.resize_with(index + 1, Vec::new);
		}

		&mut self.sections[index]
	}

	pub fn valid(&self, pipeline: PipelineHandle) -> impl Iterator<Item = u32> + '_ {
		self.section(pipeline)
			.iter()
			.copied()
			.filter(|id| id & INVALID_OBJECT == 0)
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn objects_are_listed_by_pipeline_and_can_be_invalidated() {
		let mut list = RenderList::default();

		list.add(PipelineHandle(3), 7);
		list.add(PipelineHandle(3), 8);
		list.add(PipelineHandle(1), 7);

		assert_eq!(list.count(), 3);
		assert_eq!(list.duplicates(7), 2);
		assert_eq!(list.section(PipelineHandle(9)), &[] as &[u32]);

		list.invalidate(8);

		assert_eq!(list.valid(PipelineHandle(3)).collect::<Vec<_>>(), vec![7]);

		list.clear_section(PipelineHandle(3));

		assert_eq!(list.count(), 1);
	}
}
