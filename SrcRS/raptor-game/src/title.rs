use std::path::Path;

#[derive(Default)]
pub struct TitleTracker {
	shown: Option<String>,
}

impl TitleTracker {
	pub fn update(&mut self, base_title: &str, blockout_path: &str) -> Option<String> {
		if self.shown.as_deref() == Some(blockout_path) {
			return None;
		}

		self.shown = Some(blockout_path.to_owned());

		if blockout_path.is_empty() {
			return Some(base_title.to_owned());
		}

		let file_name = Path::new(blockout_path)
			.file_name()
			.map(|name| name.to_string_lossy().into_owned())
			.unwrap_or_default();

		Some(format!("{base_title} - {file_name}"))
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn the_title_changes_only_when_the_blockout_does() {
		let mut tracker = TitleTracker::default();

		assert_eq!(tracker.update("Raptor", "").as_deref(), Some("Raptor"));
		assert_eq!(tracker.update("Raptor", ""), None);
		assert_eq!(
			tracker
				.update("Raptor", "RaptorData/Data/blockouts/a.prx")
				.as_deref(),
			Some("Raptor - a.prx")
		);
		assert_eq!(
			tracker.update("Raptor", "RaptorData/Data/blockouts/a.prx"),
			None
		);
	}
}
