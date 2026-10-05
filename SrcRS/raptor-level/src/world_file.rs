use raptor_config::host::Host;
use raptor_config::model::{Entry, Kind};

use crate::access::{Reader, find, member};

const DYNAMIC_COLLIDER: u32 = 1;

#[derive(Clone, Debug, PartialEq)]
pub struct Collider
{
	pub name: String,
	pub position: [f32; 3],
	pub rotation: [f32; 4],
	pub dynamic: bool,
	/// The size of the box, or none if the collider has no `box`. A box that does not say its `size`
	/// is one unit across.
	pub box_size: Option<[f32; 3]>,
}

/// What an object keeps from the model it was loaded with when the file does not set it
#[derive(Clone, Debug, PartialEq)]
pub struct ObjectDef
{
	pub name: String,
	pub mesh: Option<String>,
	pub shadows: Option<bool>,
	pub position: Option<[f32; 3]>,
	pub rotation: Option<[f32; 4]>,
	pub scale: Option<f32>,
	pub layer: Option<i64>,
	pub unlit: bool,
	pub no_cull: bool,
	pub collider: Option<String>,
}

#[derive(Debug, Default)]
pub struct WorldFile
{
	pub has_errors: bool,
	pub has_meta: bool,
	pub name: Option<String>,
	pub colliders: Vec<Collider>,
	pub objects: Vec<ObjectDef>,
}

fn text_of(reader: &mut Reader, entry: &Entry) -> String
{
	if entry.value.kind != Kind::String {
		reader.warn("Attempting to retrieve string from non-string config primitive");
		return String::new();
	}

	String::from_utf8_lossy(entry.value.string_value.as_deref().unwrap_or(&[])).into_owned()
}

fn read_collider(reader: &mut Reader, entry: &Entry) -> Collider
{
	let dynamic = member(entry, "type")
		.is_some_and(|kind| reader.int_of(&kind.value) as u32 == DYNAMIC_COLLIDER);

	let box_size = member(entry, "box")
		.map(|shape| reader.vec3(shape, "size").unwrap_or([1.0, 1.0, 1.0]));

	Collider {
		name: String::from_utf8_lossy(&entry.name).into_owned(),
		position: reader.vec3(entry, "pos").unwrap_or([0.0; 3]),
		rotation: reader.quat(entry, "rot").unwrap_or([0.0, 0.0, 0.0, 1.0]),
		dynamic,
		box_size,
	}
}

fn read_object(reader: &mut Reader, entry: &Entry) -> ObjectDef
{
	ObjectDef {
		name: String::from_utf8_lossy(&entry.name).into_owned(),
		mesh: member(entry, "mesh").map(|mesh| text_of(reader, mesh)),
		shadows: reader.int(entry, "shadows").map(|value| value != 0),
		position: reader.vec3(entry, "pos"),
		rotation: reader.quat(entry, "rot"),
		scale: reader.float(entry, "scale"),
		layer: reader.int(entry, "layer"),
		unlit: reader
			.int(entry, "unlit")
			.is_some_and(|value| value as i32 != 0),
		no_cull: member(entry, "nocull").is_some(),
		collider: member(entry, "collider").map(|collider| text_of(reader, collider)),
	}
}

impl WorldFile
{
	pub fn from_entries(entries: &[Entry], has_errors: bool, host: &mut dyn Host) -> Self
	{
		let mut reader = Reader::new(host);

		let meta = find(entries, "meta");

		let name = meta
			.and_then(|meta| member(meta, "name"))
			.map(|name| text_of(&mut reader, name));

		let colliders = find(entries, "colliders")
			.map(|list| {
				list.members
					.iter()
					.map(|collider| read_collider(&mut reader, collider))
					.collect()
			})
			.unwrap_or_default();

		let objects = find(entries, "objects")
			.map(|list| {
				list.members
					.iter()
					.map(|object| read_object(&mut reader, object))
					.collect()
			})
			.unwrap_or_default();

		Self {
			has_errors,
			has_meta: meta.is_some(),
			name,
			colliders,
			objects,
		}
	}

	pub fn parse(data: &[u8], prelude_path: Option<&[u8]>, host: &mut dyn Host) -> Self
	{
		let parsed = raptor_config::parse(data, prelude_path, b".conf", host);

		Self::from_entries(&parsed.entries, parsed.has_errors, host)
	}
}

#[cfg(test)]
mod tests
{
	use super::*;
	use raptor_config::host::LogLevel;

	#[derive(Default)]
	struct TestHost
	{
		messages: Vec<(LogLevel, String)>,
	}

	impl Host for TestHost
	{
		fn read_include(&mut self, _path: &[u8], _extension: &[u8]) -> Option<Vec<u8>>
		{
			Some(b"True = 1\nFalse = 0\n".to_vec())
		}

		fn log(&mut self, level: LogLevel, _category: i32, message: &[u8])
		{
			self.messages
				.push((level, String::from_utf8_lossy(message).into_owned()));
		}
	}

	fn parse(text: &str) -> (WorldFile, TestHost)
	{
		let mut host = TestHost::default();
		let world = WorldFile::parse(text.as_bytes(), Some(b"constants"), &mut host);

		(world, host)
	}

	#[test]
	fn the_demo_file_reads_as_one_object_with_a_name()
	{
		let (world, host) = parse(
			"meta = {\n    name = \"demo0\"\n}\n\n/*\ncolliders = {\n}*/\n\nobjects = {\n\textinguisher = {\n\t\tmesh = \"/FireExtinguisher_baked.glb\"\n\t\tshadows = $True\n\t\tscale = 1.5\n\t}\n}\n",
		);

		assert!(world.has_meta && !world.has_errors);
		assert_eq!(world.name.as_deref(), Some("demo0"));
		assert!(world.colliders.is_empty());

		let object = &world.objects[0];

		assert_eq!(object.name, "extinguisher");
		assert_eq!(object.mesh.as_deref(), Some("/FireExtinguisher_baked.glb"));
		assert_eq!(object.shadows, Some(true));
		assert_eq!(object.scale, Some(1.5));
		assert!(object.position.is_none() && object.rotation.is_none() && object.layer.is_none());
		assert!(!object.unlit && !object.no_cull && object.collider.is_none());
		assert!(host.messages.is_empty());
	}

	#[test]
	fn a_file_without_meta_says_so()
	{
		let (world, _) = parse("objects = {\n}\n");

		assert!(!world.has_meta && world.name.is_none());
	}

	#[test]
	fn an_object_without_a_mesh_has_none_instead_of_a_crash()
	{
		let (world, _) = parse("objects = {\n\ta = {\n\t\tpos = [ 1, 2, 3 ]\n\t}\n}\n");

		assert_eq!(world.objects[0].mesh, None);
		assert_eq!(world.objects[0].position, Some([1.0, 2.0, 3.0]));
	}

	#[test]
	fn a_file_without_objects_has_an_empty_list()
	{
		let (world, _) = parse("meta = {\n\tname = \"x\"\n}\n");

		assert!(world.objects.is_empty() && world.colliders.is_empty());
	}

	#[test]
	fn an_object_can_override_every_property()
	{
		let (world, _) = parse(
			"objects = {\n\ta = {\n\t\tmesh = \"/a.glb\"\n\t\tshadows = $False\n\t\tpos = [ 1, 2, 3 ]\n\t\trot = [ 0, 0, 0, 1 ]\n\t\tscale = 2\n\t\tlayer = 1\n\t\tunlit = $True\n\t\tnocull = $True\n\t\tcollider = \"floor\"\n\t}\n}\n",
		);

		let object = &world.objects[0];

		assert_eq!(object.shadows, Some(false));
		assert_eq!(object.rotation, Some([0.0, 0.0, 0.0, 1.0]));
		assert_eq!(object.scale, Some(2.0));
		assert_eq!(object.layer, Some(1));
		assert!(object.unlit && object.no_cull);
		assert_eq!(object.collider.as_deref(), Some("floor"));
	}

	#[test]
	fn unlit_is_set_by_any_nonzero_number()
	{
		let (world, _) = parse("objects = {\n\ta = {\n\t\tunlit = 3\n\t}\n\tb = {\n\t\tunlit = 0\n\t}\n}\n");

		assert!(world.objects[0].unlit);
		assert!(!world.objects[1].unlit);
	}

	#[test]
	fn a_collider_reads_its_transform_type_and_box()
	{
		let (world, _) = parse(
			"colliders = {\n\tfloor = {\n\t\tpos = [ 0, -1.5, 0 ]\n\t\tbox = { size = [ 64, 1, 64 ] }\n\t}\n\tcrate = {\n\t\ttype = 1\n\t\trot = [ 0, 1, 0, 0 ]\n\t\tbox = {\n\t\t}\n\t}\n\tmarker = {\n\t}\n}\n",
		);

		assert_eq!(world.colliders.len(), 3);

		let floor = &world.colliders[0];
		assert_eq!(floor.position, [0.0, -1.5, 0.0]);
		assert_eq!(floor.rotation, [0.0, 0.0, 0.0, 1.0]);
		assert!(!floor.dynamic);
		assert_eq!(floor.box_size, Some([64.0, 1.0, 64.0]));

		let crate_collider = &world.colliders[1];
		assert!(crate_collider.dynamic);
		assert_eq!(crate_collider.rotation, [0.0, 1.0, 0.0, 0.0]);
		assert_eq!(crate_collider.box_size, Some([1.0, 1.0, 1.0]));

		assert_eq!(world.colliders[2].box_size, None);
	}

	#[test]
	fn the_box_size_key_is_case_sensitive()
	{
		let (world, _) = parse("colliders = {\n\tfloor = {\n\t\tbox = { Size = [ 64, 1, 64 ] }\n\t}\n}\n");

		assert_eq!(world.colliders[0].box_size, Some([1.0, 1.0, 1.0]));
	}

	#[test]
	fn a_mesh_that_is_not_text_warns_and_reads_as_empty()
	{
		let (world, host) = parse("objects = {\n\ta = {\n\t\tmesh = 5\n\t}\n}\n");

		assert_eq!(world.objects[0].mesh.as_deref(), Some(""));
		assert!(host.messages.iter().any(|(level, _)| *level == LogLevel::Warning));
	}
}
