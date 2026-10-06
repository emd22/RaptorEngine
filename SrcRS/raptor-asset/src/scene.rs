use raptor_core::log_error;
use raptor_level::{Collider, ObjectDef, WorldFile};

use crate::fs::{ConfigHost, config_constants_path, read_resolved};

/// What loading a scene file does to the game
pub trait SceneHost {
	/// Whether a scene has been loaded already. Objects blockouts attach do not count.
	fn is_populated(&self) -> bool;

	fn set_scene_path(&mut self, path: &str);

	fn set_world_name(&mut self, name: &str);

	fn mark_populated(&mut self);

	fn add_collider(&mut self, collider: &Collider);

	/// Loads the model at `model_path` into a new object and applies the properties the file sets
	fn add_object(&mut self, model_path: &str, object: &ObjectDef);

	/// Applies the properties the file sets to the object of that name. False if there is none.
	fn update_object(&mut self, object: &ObjectDef) -> bool;
}

#[derive(Debug, PartialEq, Eq)]
pub enum SceneError {
	NoMetadata,
	Unreadable(String),
}

impl std::fmt::Display for SceneError {
	fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
		match self {
			Self::NoMetadata => f.write_str("Project missing metadata!"),
			Self::Unreadable(path) => write!(f, "The scene file '{path}' could not be read"),
		}
	}
}

pub fn model_path(scene_path: &str, mesh: &str) -> String {
	format!("{scene_path}/Models{mesh}")
}

pub fn read_world_file(path: &str) -> Result<WorldFile, SceneError> {
	let bytes = read_resolved(path).map_err(|_| SceneError::Unreadable(path.to_owned()))?;
	let constants = config_constants_path();

	Ok(WorldFile::parse(
		&bytes,
		Some(constants.to_string_lossy().as_bytes()),
		&mut ConfigHost,
	))
}

/// Applies a scene file: the first time it makes the colliders and objects, and after that it only
/// updates the objects it already made
pub fn apply_world_file(
	host: &mut impl SceneHost,
	scene_path: &str,
	info: &WorldFile,
) -> Result<(), SceneError> {
	let first_time = !host.is_populated();

	if first_time {
		if !info.has_meta {
			return Err(SceneError::NoMetadata);
		}

		host.set_world_name(info.name.as_deref().unwrap_or(""));

		for collider in &info.colliders {
			host.add_collider(collider);
		}
	}

	for object in &info.objects {
		if first_time || !host.update_object(object) {
			add_object(host, scene_path, object);
		}
	}

	if first_time {
		host.mark_populated();
	}

	Ok(())
}

fn add_object(host: &mut impl SceneHost, scene_path: &str, object: &ObjectDef) {
	let Some(mesh) = &object.mesh else {
		log_error!(Asset; "Object '{}' has no mesh, skipping it", object.name);
		return;
	};

	host.add_object(&model_path(scene_path, mesh), object);
}

/// Loads `<path>/info.prx`
pub fn load_scene(host: &mut impl SceneHost, path: &str) -> Result<(), SceneError> {
	host.set_scene_path(path);

	let info = read_world_file(&format!("{path}/info.prx"))?;

	apply_world_file(host, path, &info)
}

#[cfg(test)]
mod tests {
	use super::*;

	#[derive(Default)]
	struct Recorder {
		populated: bool,
		known: Vec<String>,
		events: Vec<String>,
	}

	impl SceneHost for Recorder {
		fn is_populated(&self) -> bool {
			self.populated
		}

		fn set_scene_path(&mut self, path: &str) {
			self.events.push(format!("path {path}"));
		}

		fn set_world_name(&mut self, name: &str) {
			self.events.push(format!("name {name}"));
		}

		fn mark_populated(&mut self) {
			self.populated = true;
		}

		fn add_collider(&mut self, collider: &Collider) {
			self.events.push(format!("collider {}", collider.name));
		}

		fn add_object(&mut self, model_path: &str, object: &ObjectDef) {
			self.known.push(object.name.clone());
			self.events
				.push(format!("object {} {model_path}", object.name));
		}

		fn update_object(&mut self, object: &ObjectDef) -> bool {
			let found = self.known.contains(&object.name);

			if found {
				self.events.push(format!("update {}", object.name));
			}

			found
		}
	}

	fn info(text: &str) -> WorldFile {
		WorldFile::parse(text.as_bytes(), None, &mut ConfigHost)
	}

	const SCENE: &str = "meta = {\n\tname = \"demo\"\n}\ncolliders = {\n\tfloor = {\n\t}\n}\nobjects = {\n\ta = {\n\t\tmesh = \"/a.glb\"\n\t}\n\tb = {\n\t}\n}\n";

	#[test]
	fn the_first_load_makes_colliders_and_objects_and_skips_objects_without_a_mesh() {
		let mut host = Recorder::default();

		apply_world_file(&mut host, "scene", &info(SCENE)).unwrap();

		assert_eq!(
			host.events,
			vec!["name demo", "collider floor", "object a scene/Models/a.glb"]
		);
		assert!(host.populated);
	}

	#[test]
	fn a_reload_updates_known_objects_and_adds_new_ones() {
		let mut host = Recorder::default();

		apply_world_file(&mut host, "scene", &info(SCENE)).unwrap();
		host.events.clear();

		let more = format!("{SCENE}");
		let more = more.replace(
			"b = {\n\t}",
			"b = {\n\t}\n\tc = {\n\t\tmesh = \"/c.glb\"\n\t}",
		);

		apply_world_file(&mut host, "scene", &info(&more)).unwrap();

		assert_eq!(host.events, vec!["update a", "object c scene/Models/c.glb"]);
	}

	#[test]
	fn a_first_load_without_metadata_is_refused() {
		let mut host = Recorder::default();

		assert_eq!(
			apply_world_file(&mut host, "scene", &info("objects = {\n}\n")),
			Err(SceneError::NoMetadata)
		);
		assert!(!host.populated);
	}
}
