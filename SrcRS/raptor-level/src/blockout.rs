use raptor_config::host::Host;
use raptor_config::model::{Entry, Primitive};

use crate::access::{
	Reader, dot_reference, find, float_array, int_array, literal, member, structure,
};

pub const MAX_PLANES: usize = 32;

const VALUES_PER_FACE_TEXTURE: usize = 5;

const POINT_LIGHT: i64 = 0;
const SPOT_LIGHT: i64 = 1;

const DEFAULT_LIGHT_RADIUS: f32 = 5.0;
const DEFAULT_SPOT_INNER: f32 = 20.0;
const DEFAULT_SPOT_OUTER: f32 = 30.0;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Texture
{
	pub offset: [f32; 2],
	pub scale: [f32; 2],
	pub rotation: f32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct PlaneDef
{
	pub normal: [f32; 3],
	pub distance: f32,
	pub texture: Option<Texture>,
}

#[derive(Clone, Debug, PartialEq)]
pub enum BrushSource
{
	/// Not a brush that can be made, because the entry is missing its shape or has too many planes
	Invalid,
	Box
	{
		min: [f32; 3], max: [f32; 3]
	},
	/// `has_textures` is false when the file gave no layout, and the faces should get the default one
	Planes
	{
		planes: Vec<PlaneDef>,
		has_textures: bool,
	},
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Rotation
{
	Identity,
	/// Angles of the old format, still to be turned into a quaternion
	Euler([f32; 3]),
	Quat([f32; 4]),
}

#[derive(Clone, Debug, PartialEq)]
pub struct Block
{
	pub name: String,
	pub position: [f32; 3],
	pub brush: BrushSource,
	pub rotation: Rotation,
	pub locked: bool,
	pub material: Option<i32>,
	pub probe_volume: bool,
	pub reflection_probe: bool,
	pub bleeds: bool,
	pub dynamic: bool,
}

/// What a light that the file does not set keeps from the light that is already there
#[derive(Clone, Debug, PartialEq)]
pub struct Sun
{
	pub enabled: bool,
	pub position: [f32; 3],
	pub color: Option<[i32; 3]>,
	pub intensity: Option<f32>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LightKind
{
	Point,
	Spot,
	Unknown(i64),
}

#[derive(Clone, Debug, PartialEq)]
pub struct Light
{
	pub name: String,
	pub kind: LightKind,
	pub position: Option<[f32; 3]>,
	pub color: Option<[i32; 3]>,
	pub intensity: Option<f32>,
	pub radius: f32,
	pub direction: Option<[f32; 3]>,
	pub rotation: Option<[f32; 4]>,
	pub inner_degrees: f32,
	pub outer_degrees: f32,
	pub shadows: bool,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Camera
{
	pub aperture: Option<f32>,
	pub shutter: Option<f32>,
	pub iso: Option<f32>,
	pub exposure_ev: Option<f32>,
}

#[derive(Debug, Default)]
pub struct Level
{
	pub has_errors: bool,
	pub has_blocks: bool,
	pub sun: Option<Sun>,
	pub lights: Vec<Light>,
	pub camera: Option<Camera>,
	pub blocks: Vec<Block>,
}

fn read_texture_values(values: &[f32]) -> Texture
{
	Texture {
		offset: [values[0], values[1]],
		scale: [values[2], values[3]],
		rotation: values[4],
	}
}

fn read_brush(reader: &mut Reader, entry: &Entry) -> BrushSource
{
	if let Some(scale) = member(entry, "scale") {
		if scale.array.len() < 6 {
			return BrushSource::Invalid;
		}

		let s: Vec<f32> = scale
			.array
			.iter()
			.take(6)
			.map(|value| reader.float_of(value))
			.collect();

		return BrushSource::Box {
			min: [-s[0], -s[3], -s[5]],
			max: [s[1], s[2], s[4]],
		};
	}

	let Some(planes) = member(entry, "planes") else {
		return BrushSource::Invalid;
	};

	let plane_count = planes.array.len() / 4;

	if plane_count > MAX_PLANES {
		reader.error(&format!(
			"Blockout '{}' has {plane_count} planes, the maximum is {MAX_PLANES}",
			String::from_utf8_lossy(&entry.name)
		));
		return BrushSource::Invalid;
	}

	let values: Vec<f32> = planes
		.array
		.iter()
		.take(plane_count * 4)
		.map(|value| reader.float_of(value))
		.collect();

	let mut planes: Vec<PlaneDef> = values
		.as_chunks::<4>()
		.0
		.iter()
		.map(|chunk| PlaneDef {
			normal: [chunk[0], chunk[1], chunk[2]],
			distance: chunk[3],
			texture: None,
		})
		.collect();

	let Some(uvs) = member(entry, "uvs") else {
		return BrushSource::Planes {
			planes,
			has_textures: false,
		};
	};

	for (index, plane) in planes.iter_mut().enumerate() {
		let base = index * VALUES_PER_FACE_TEXTURE;

		if base + VALUES_PER_FACE_TEXTURE > uvs.array.len() {
			break;
		}

		let values: Vec<f32> = uvs.array[base..base + VALUES_PER_FACE_TEXTURE]
			.iter()
			.map(|value| reader.float_of(value))
			.collect();

		plane.texture = Some(read_texture_values(&values));
	}

	BrushSource::Planes {
		planes,
		has_textures: true,
	}
}

fn read_block(reader: &mut Reader, entry: &Entry) -> Block
{
	let rotation = if let Some(quat) = reader.quat(entry, "rotquat") {
		Rotation::Quat(quat)
	}
	else if let Some(euler) = reader.vec3(entry, "rot") {
		Rotation::Euler(euler)
	}
	else {
		Rotation::Identity
	};

	Block {
		name: String::from_utf8_lossy(&entry.name).into_owned(),
		position: reader.vec3(entry, "pos").unwrap_or([0.0; 3]),
		brush: read_brush(reader, entry),
		rotation,
		locked: reader.flag(entry, "lock"),
		material: member(entry, "mat").map(|mat| reader.int_of(&mat.value) as i32),
		probe_volume: reader.flag(entry, "probevolume"),
		reflection_probe: reader.flag(entry, "reflectionprobe"),
		bleeds: reader.flag(entry, "bleeds"),
		dynamic: reader.flag(entry, "dynamic"),
	}
}

fn read_color(reader: &mut Reader, entry: &Entry) -> Option<[i32; 3]>
{
	reader
		.color(entry, "color")
		.map(|color| [color[0], color[1], color[2]])
}

fn read_light(reader: &mut Reader, entry: &Entry) -> Light
{
	let kind = match reader.int(entry, "type").unwrap_or(POINT_LIGHT) {
		POINT_LIGHT => LightKind::Point,
		SPOT_LIGHT => LightKind::Spot,
		other => LightKind::Unknown(other),
	};

	Light {
		name: String::from_utf8_lossy(&entry.name).into_owned(),
		kind,
		position: reader.vec3(entry, "pos"),
		color: read_color(reader, entry),
		intensity: reader.float(entry, "intensity"),
		radius: reader
			.float(entry, "radius")
			.unwrap_or(DEFAULT_LIGHT_RADIUS),
		direction: reader.vec3(entry, "dir"),
		rotation: reader.quat(entry, "rot"),
		inner_degrees: reader
			.float(entry, "inner")
			.unwrap_or(DEFAULT_SPOT_INNER),
		outer_degrees: reader
			.float(entry, "outer")
			.unwrap_or(DEFAULT_SPOT_OUTER),
		shadows: reader.int(entry, "shadows").is_none_or(|value| value != 0),
	}
}

impl Level
{
	pub fn from_entries(entries: &[Entry], has_errors: bool, host: &mut dyn Host) -> Self
	{
		let mut reader = Reader::new(host);

		let sun = find(entries, "sun").map(|entry| Sun {
			enabled: reader.int(entry, "enabled").is_none_or(|value| value != 0),
			position: reader.vec3(entry, "pos").unwrap_or([0.0; 3]),
			color: read_color(&mut reader, entry),
			intensity: reader.float(entry, "intensity"),
		});

		let lights = find(entries, "lights")
			.map(|list| {
				list.members
					.iter()
					.map(|light| read_light(&mut reader, light))
					.collect()
			})
			.unwrap_or_default();

		let camera = find(entries, "camera").map(|entry| Camera {
			aperture: reader.float(entry, "aperture"),
			shutter: reader.float(entry, "shutter"),
			iso: reader.float(entry, "iso"),
			exposure_ev: reader.float(entry, "exposure_ev"),
		});

		let blocks_entry = find(entries, "all");

		let blocks = blocks_entry
			.map(|list| {
				list.members
					.iter()
					.map(|block| read_block(&mut reader, block))
					.collect()
			})
			.unwrap_or_default();

		Self {
			has_errors,
			has_blocks: blocks_entry.is_some(),
			sun,
			lights,
			camera,
			blocks,
		}
	}

	/// Parses level text. `prelude_path` is the file of constants (`$CLight.Spot` and the like) the
	/// level may refer to.
	pub fn parse(data: &[u8], prelude_path: Option<&[u8]>, host: &mut dyn Host) -> Self
	{
		let parsed = raptor_config::parse(data, prelude_path, b".conf", host);

		Self::from_entries(&parsed.entries, parsed.has_errors, host)
	}
}

#[derive(Clone, Debug, PartialEq)]
pub struct SunOut
{
	pub enabled: bool,
	pub position: [f32; 3],
	pub color: [i32; 4],
	pub intensity: f32,
}

#[derive(Clone, Debug, PartialEq)]
pub struct LightOut
{
	pub name: String,
	pub spot: bool,
	pub position: [f32; 3],
	pub color: [i32; 4],
	pub intensity: f32,
	pub radius: f32,
	pub direction: [f32; 3],
	pub inner_degrees: f32,
	pub outer_degrees: f32,
	pub shadows: bool,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CameraOut
{
	pub aperture: f32,
	pub shutter: f32,
	pub iso: f32,
	pub exposure_ev: f32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct PlaneOut
{
	pub normal: [f32; 3],
	pub distance: f32,
	pub texture: Texture,
}

#[derive(Clone, Debug, PartialEq)]
pub enum BrushOut
{
	/// Extents in the order left, right, top, bottom, front, back
	Box([f32; 6]),
	/// `textures` is false when every face has the default layout, and the layout is left out
	Planes
	{
		planes: Vec<PlaneOut>,
		textures: bool,
	},
}

#[derive(Clone, Debug, PartialEq)]
pub struct BlockOut
{
	pub name: String,
	pub position: [f32; 3],
	pub brush: BrushOut,
	pub rotation: [f32; 4],
	pub locked: bool,
	pub probe_volume: bool,
	pub reflection_probe: bool,
	pub bleeds: bool,
	pub dynamic: bool,
	pub material: Option<i32>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct LevelOut
{
	pub sun: Option<SunOut>,
	pub lights: Vec<LightOut>,
	pub camera: CameraOut,
	pub blocks: Vec<BlockOut>,
}

fn flag_entry(name: &str) -> Entry
{
	literal(name, Primitive::int(1))
}

fn brush_entries(brush: &BrushOut, out: &mut Vec<Entry>)
{
	match brush {
		BrushOut::Box(extents) => out.push(float_array("scale", extents)),
		BrushOut::Planes { planes, textures } => {
			let values: Vec<f32> = planes
				.iter()
				.flat_map(|plane| {
					[
						plane.normal[0],
						plane.normal[1],
						plane.normal[2],
						plane.distance,
					]
				})
				.collect();

			out.push(float_array("planes", &values));

			if *textures {
				let values: Vec<f32> = planes
					.iter()
					.flat_map(|plane| {
						[
							plane.texture.offset[0],
							plane.texture.offset[1],
							plane.texture.scale[0],
							plane.texture.scale[1],
							plane.texture.rotation,
						]
					})
					.collect();

				out.push(float_array("uvs", &values));
			}
		}
	}
}

impl BlockOut
{
	fn to_entry(&self) -> Entry
	{
		let mut members = vec![float_array("pos", &self.position)];

		brush_entries(&self.brush, &mut members);

		members.push(float_array("rotquat", &self.rotation));

		for (set, name) in [
			(self.locked, "lock"),
			(self.probe_volume, "probevolume"),
			(self.reflection_probe, "reflectionprobe"),
			(self.bleeds, "bleeds"),
			(self.dynamic, "dynamic"),
		] {
			if set {
				members.push(flag_entry(name));
			}
		}

		if let Some(material) = self.material {
			members.push(literal("mat", Primitive::int(i64::from(material))));
		}

		let mut entry = structure(&self.name);

		for member in members {
			entry.add_member(member);
		}

		entry
	}
}

impl LightOut
{
	fn to_entry(&self) -> Entry
	{
		let mut entry = structure(&self.name);

		entry.add_member(dot_reference(
			"type",
			if self.spot {
				"$CLight.Spot"
			}
			else {
				"$CLight.Point"
			},
		));
		entry.add_member(float_array("pos", &self.position));
		entry.add_member(int_array("color", &self.color));
		entry.add_member(literal("intensity", Primitive::float(self.intensity)));
		entry.add_member(literal("radius", Primitive::float(self.radius)));

		if self.spot {
			entry.add_member(float_array("dir", &self.direction));
			entry.add_member(literal("inner", Primitive::float(self.inner_degrees)));
			entry.add_member(literal("outer", Primitive::float(self.outer_degrees)));
			entry.add_member(literal(
				"shadows",
				Primitive::int(i64::from(self.shadows)),
			));
		}

		entry
	}
}

impl LevelOut
{
	/// The entries of a level file, in the order the engine writes them
	pub fn to_entries(&self) -> Vec<Entry>
	{
		let mut entries = Vec::new();

		if let Some(sun) = &self.sun {
			let mut entry = structure("sun");

			entry.add_member(literal("enabled", Primitive::int(i64::from(sun.enabled))));
			entry.add_member(float_array("pos", &sun.position));
			entry.add_member(int_array("color", &sun.color));
			entry.add_member(literal("intensity", Primitive::float(sun.intensity)));

			entries.push(entry);
		}

		let mut lights = structure("lights");

		for light in &self.lights {
			lights.add_member(light.to_entry());
		}

		entries.push(lights);

		let mut camera = structure("camera");

		camera.add_member(literal("aperture", Primitive::float(self.camera.aperture)));
		camera.add_member(literal("shutter", Primitive::float(self.camera.shutter)));
		camera.add_member(literal("iso", Primitive::float(self.camera.iso)));
		camera.add_member(literal(
			"exposure_ev",
			Primitive::float(self.camera.exposure_ev),
		));

		entries.push(camera);

		let mut blocks = Entry {
			name: b"all".to_vec(),
			..Entry::default()
		};

		for block in &self.blocks {
			blocks.add_member(block.to_entry());
		}

		entries.push(blocks);

		entries
	}

	pub fn to_text(&self, host: &mut dyn Host) -> Vec<u8>
	{
		raptor_config::writer::format_file(&self.to_entries(), host)
	}
}

#[cfg(test)]
mod tests
{
	use super::*;
	use raptor_config::host::LogLevel;

	const CONSTANTS: &[u8] = b"True = 1\nFalse = 0\nCLight = {\n\tPoint = 0\n\tSpot = 1\n}\n";

	#[derive(Default)]
	struct TestHost
	{
		messages: Vec<(LogLevel, String)>,
	}

	impl Host for TestHost
	{
		fn read_include(&mut self, _path: &[u8], _extension: &[u8]) -> Option<Vec<u8>>
		{
			Some(CONSTANTS.to_vec())
		}

		fn log(&mut self, level: LogLevel, _category: i32, message: &[u8])
		{
			self.messages
				.push((level, String::from_utf8_lossy(message).into_owned()));
		}
	}

	fn parse(text: &str) -> (Level, TestHost)
	{
		let mut host = TestHost::default();
		let level = Level::parse(text.as_bytes(), Some(b"constants"), &mut host);

		(level, host)
	}

	fn sample_out() -> LevelOut
	{
		let texture = Texture {
			offset: [0.5, 0.0],
			scale: [2.0, 2.0],
			rotation: 90.0,
		};

		LevelOut {
			sun: Some(SunOut {
				enabled: false,
				position: [2.5, 1.5, -1.5],
				color: [250, 180, 142, 255],
				intensity: 50000.0,
			}),
			lights: vec![
				LightOut {
					name: "Light_1".to_owned(),
					spot: true,
					position: [-6.5, 3.25, 1.0],
					color: [131, 255, 253, 255],
					intensity: 100000.0,
					radius: 8.0,
					direction: [0.0, -1.0, 0.25],
					inner_degrees: 60.0,
					outer_degrees: 90.0,
					shadows: true,
				},
				LightOut {
					name: "Lamp".to_owned(),
					spot: false,
					position: [0.0, 1.0, 2.0],
					color: [255, 255, 255, 255],
					intensity: 800.0,
					radius: 4.0,
					direction: [0.0; 3],
					inner_degrees: 0.0,
					outer_degrees: 0.0,
					shadows: false,
				},
			],
			camera: CameraOut {
				aperture: 16.0,
				shutter: 0.01,
				iso: 100.0,
				exposure_ev: 0.0,
			},
			blocks: vec![
				BlockOut {
					name: "0".to_owned(),
					position: [1.0, 2.0, 3.0],
					brush: BrushOut::Box([0.5, 0.5, 0.25, 0.25, 1.0, 1.0]),
					rotation: [0.0, 0.0, 0.0, 1.0],
					locked: true,
					probe_volume: false,
					reflection_probe: false,
					bleeds: false,
					dynamic: false,
					material: Some(2),
				},
				BlockOut {
					name: "wedge".to_owned(),
					position: [0.0; 3],
					brush: BrushOut::Planes {
						planes: vec![
							PlaneOut {
								normal: [1.0, 0.0, 0.0],
								distance: 0.5,
								texture,
							},
							PlaneOut {
								normal: [-1.0, 0.0, 0.0],
								distance: 0.5,
								texture,
							},
						],
						textures: true,
					},
					rotation: [0.0, 0.70710677, 0.0, 0.70710677],
					locked: false,
					probe_volume: true,
					reflection_probe: true,
					bleeds: true,
					dynamic: true,
					material: None,
				},
			],
		}
	}

	#[test]
	fn a_level_is_written_in_the_engine_layout()
	{
		let mut host = TestHost::default();

		let text = String::from_utf8(sample_out().to_text(&mut host)).unwrap();

		let expected = "sun = {\n\
			\tenabled = 0\n\
			\tpos = [ 2.500000, 1.500000, -1.500000 ]\n\
			\tcolor = [ 250, 180, 142, 255 ]\n\
			\tintensity = 50000.000000\n\
			}\n\
			lights = {\n\
			\tLight_1 = {\n\
			\t\ttype = $CLight.Spot\n";

		assert!(text.starts_with(expected), "{text}");
		assert!(text.contains("\tLamp = {\n\t\ttype = $CLight.Point\n"));
		assert!(text.contains("camera = {\n\taperture = 16.000000\n"));
		assert!(text.contains("\t\tscale = [ 0.500000, 0.500000, 0.250000, 0.250000, 1.000000, 1.000000 ]\n"));
		assert!(text.contains("\t\tlock = 1\n\t\tmat = 2\n"));
		assert!(text.contains("\t\tprobevolume = 1\n\t\treflectionprobe = 1\n\t\tbleeds = 1\n\t\tdynamic = 1\n"));
		assert!(host.messages.is_empty());
	}

	#[test]
	fn reading_what_was_written_gives_the_same_level()
	{
		let out = sample_out();
		let mut host = TestHost::default();
		let text = out.to_text(&mut host);

		let mut host = TestHost::default();
		let level = Level::parse(&text, Some(b"constants"), &mut host);

		assert!(!level.has_errors && level.has_blocks);
		assert!(host.messages.is_empty(), "{:?}", host.messages);

		let sun = level.sun.unwrap();
		assert!(!sun.enabled);
		assert_eq!(sun.position, [2.5, 1.5, -1.5]);
		assert_eq!(sun.color, Some([250, 180, 142]));
		assert_eq!(sun.intensity, Some(50000.0));

		assert_eq!(level.lights.len(), 2);
		let spot = &level.lights[0];
		assert_eq!(spot.kind, LightKind::Spot);
		assert_eq!(spot.position, Some([-6.5, 3.25, 1.0]));
		assert_eq!(spot.direction, Some([0.0, -1.0, 0.25]));
		assert_eq!((spot.inner_degrees, spot.outer_degrees), (60.0, 90.0));
		assert!(spot.shadows);
		assert_eq!(level.lights[1].kind, LightKind::Point);

		assert_eq!(level.camera.unwrap().aperture, Some(16.0));

		assert_eq!(level.blocks.len(), 2);

		let first = &level.blocks[0];
		assert_eq!(first.name, "0");
		assert_eq!(first.position, [1.0, 2.0, 3.0]);
		assert_eq!(
			first.brush,
			BrushSource::Box {
				min: [-0.5, -0.25, -1.0],
				max: [0.5, 0.25, 1.0],
			}
		);
		assert!(first.locked && !first.dynamic);
		assert_eq!(first.material, Some(2));
		assert_eq!(first.rotation, Rotation::Quat([0.0, 0.0, 0.0, 1.0]));

		let second = &level.blocks[1];
		assert!(second.probe_volume && second.reflection_probe && second.bleeds && second.dynamic);
		assert_eq!(second.material, None);

		let BrushSource::Planes { planes, has_textures } = &second.brush else {
			panic!("{:?}", second.brush);
		};

		assert!(*has_textures);
		assert_eq!(planes.len(), 2);
		assert_eq!(planes[1].normal, [-1.0, 0.0, 0.0]);
		assert_eq!(planes[1].texture.unwrap().rotation, 90.0);
	}

	#[test]
	fn rewriting_a_level_that_was_read_gives_the_same_text()
	{
		let mut host = TestHost::default();
		let text = sample_out().to_text(&mut host);

		let level = Level::parse(&text, Some(b"constants"), &mut host);

		let out = LevelOut {
			sun: level.sun.map(|sun| SunOut {
				enabled: sun.enabled,
				position: sun.position,
				color: [sun.color.unwrap()[0], sun.color.unwrap()[1], sun.color.unwrap()[2], 255],
				intensity: sun.intensity.unwrap(),
			}),
			lights: level
				.lights
				.iter()
				.map(|light| LightOut {
					name: light.name.clone(),
					spot: light.kind == LightKind::Spot,
					position: light.position.unwrap(),
					color: {
						let c = light.color.unwrap();
						[c[0], c[1], c[2], 255]
					},
					intensity: light.intensity.unwrap(),
					radius: light.radius,
					direction: light.direction.unwrap_or([0.0; 3]),
					inner_degrees: light.inner_degrees,
					outer_degrees: light.outer_degrees,
					shadows: light.shadows,
				})
				.collect(),
			camera: {
				let camera = level.camera.unwrap();

				CameraOut {
					aperture: camera.aperture.unwrap(),
					shutter: camera.shutter.unwrap(),
					iso: camera.iso.unwrap(),
					exposure_ev: camera.exposure_ev.unwrap(),
				}
			},
			blocks: sample_out().blocks,
		};

		let mut other = TestHost::default();

		let rewritten = out.to_text(&mut other);

		let mut first = TestHost::default();
		let original = sample_out().to_text(&mut first);

		assert_eq!(original, rewritten);
	}

	#[test]
	fn an_empty_level_still_writes_every_section()
	{
		let out = LevelOut {
			sun: None,
			lights: vec![],
			camera: CameraOut {
				aperture: 16.0,
				shutter: 0.01,
				iso: 100.0,
				exposure_ev: 0.0,
			},
			blocks: vec![],
		};

		let mut host = TestHost::default();
		let text = String::from_utf8(out.to_text(&mut host)).unwrap();

		assert!(text.starts_with("lights = {\n}\ncamera = {\n"));
		assert!(text.ends_with("all = {\n}\n"), "{text}");

		let level = Level::parse(text.as_bytes(), Some(b"constants"), &mut host);

		assert!(level.has_blocks && level.blocks.is_empty() && level.sun.is_none());
	}

	#[test]
	fn a_file_without_an_all_entry_has_no_blocks()
	{
		let (level, _) = parse("lights = {\n}\n");

		assert!(!level.has_blocks);
	}

	#[test]
	fn the_old_box_format_gives_extents_in_each_direction()
	{
		let (level, _) = parse("all = {\n\ta = {\n\t\tscale = [ 1, 2, 3, 4, 5, 6 ]\n\t}\n}\n");

		assert_eq!(
			level.blocks[0].brush,
			BrushSource::Box {
				min: [-1.0, -4.0, -6.0],
				max: [2.0, 3.0, 5.0],
			}
		);
	}

	#[test]
	fn a_box_with_too_few_extents_is_invalid()
	{
		let (level, _) = parse("all = {\n\ta = {\n\t\tscale = [ 1, 2, 3 ]\n\t}\n}\n");

		assert_eq!(level.blocks[0].brush, BrushSource::Invalid);
	}

	#[test]
	fn a_block_with_no_shape_is_invalid()
	{
		let (level, _) = parse("all = {\n\ta = {\n\t\tpos = [ 0, 0, 0 ]\n\t}\n}\n");

		assert_eq!(level.blocks[0].brush, BrushSource::Invalid);
	}

	#[test]
	fn planes_without_a_layout_ask_for_the_default_one()
	{
		let (level, _) = parse(
			"all = {\n\ta = {\n\t\tplanes = [ 1, 0, 0, 0.5, -1, 0, 0, 0.5 ]\n\t}\n}\n",
		);

		let BrushSource::Planes { planes, has_textures } = &level.blocks[0].brush else {
			panic!();
		};

		assert!(!has_textures);
		assert_eq!(planes.len(), 2);
		assert!(planes.iter().all(|plane| plane.texture.is_none()));
	}

	#[test]
	fn a_layout_that_covers_only_some_planes_leaves_the_rest_with_the_default()
	{
		let (level, _) = parse(
			"all = {\n\ta = {\n\t\tplanes = [ 1, 0, 0, 0.5, -1, 0, 0, 0.5, 0, 1, 0, 0.5 ]\n\t\tuvs = [ 1, 2, 3, 4, 5, 6, 7, 8 ]\n\t}\n}\n",
		);

		let BrushSource::Planes { planes, has_textures } = &level.blocks[0].brush else {
			panic!();
		};

		assert!(has_textures);
		assert_eq!(
			planes[0].texture,
			Some(Texture {
				offset: [1.0, 2.0],
				scale: [3.0, 4.0],
				rotation: 5.0,
			})
		);
		assert!(planes[1].texture.is_none());
		assert!(planes[2].texture.is_none());
	}

	#[test]
	fn more_planes_than_a_brush_can_hold_is_an_error()
	{
		let values = vec!["0, 0, 1, 1"; MAX_PLANES + 1].join(", ");
		let (level, host) = parse(&format!("all = {{\n\ta = {{\n\t\tplanes = [ {values} ]\n\t}}\n}}\n"));

		assert_eq!(level.blocks[0].brush, BrushSource::Invalid);
		assert!(host.messages.iter().any(|(level, text)| *level == LogLevel::Error && text.contains("33 planes")));
	}

	#[test]
	fn a_rotation_quaternion_wins_over_euler_angles()
	{
		let (level, _) = parse(
			"all = {\n\ta = {\n\t\trot = [ 1, 2, 3 ]\n\t\trotquat = [ 0, 0, 0, 1 ]\n\t}\n\tb = {\n\t\trot = [ 1, 2, 3 ]\n\t}\n\tc = {\n\t}\n}\n",
		);

		assert_eq!(level.blocks[0].rotation, Rotation::Quat([0.0, 0.0, 0.0, 1.0]));
		assert_eq!(level.blocks[1].rotation, Rotation::Euler([1.0, 2.0, 3.0]));
		assert_eq!(level.blocks[2].rotation, Rotation::Identity);
	}

	#[test]
	fn flags_are_set_only_by_the_number_one()
	{
		let (level, _) = parse(
			"all = {\n\ta = {\n\t\tlock = 1\n\t\tdynamic = 0\n\t\tbleeds = 2\n\t\tprobevolume = 1.0\n\t}\n}\n",
		);

		let block = &level.blocks[0];

		assert!(block.locked && block.probe_volume);
		assert!(!block.dynamic && !block.bleeds && !block.reflection_probe);
	}

	#[test]
	fn a_malformed_position_warns_and_reads_as_zero()
	{
		let (level, host) = parse("all = {\n\ta = {\n\t\tpos = [ 1, 2 ]\n\t}\n}\n");

		assert_eq!(level.blocks[0].position, [0.0; 3]);
		assert!(host.messages.iter().any(|(level, _)| *level == LogLevel::Warning));
	}

	#[test]
	fn a_light_that_sets_little_keeps_defaults_and_leaves_the_rest_to_the_engine()
	{
		let (level, _) = parse("lights = {\n\ta = {\n\t\ttype = $CLight.Spot\n\t}\n\tb = {\n\t}\n\tc = {\n\t\ttype = 7\n\t}\n}\n");

		let a = &level.lights[0];
		assert_eq!(a.kind, LightKind::Spot);
		assert_eq!((a.radius, a.inner_degrees, a.outer_degrees), (5.0, 20.0, 30.0));
		assert!(a.shadows);
		assert!(a.position.is_none() && a.color.is_none() && a.intensity.is_none());
		assert!(a.direction.is_none() && a.rotation.is_none());

		assert_eq!(level.lights[1].kind, LightKind::Point);
		assert_eq!(level.lights[2].kind, LightKind::Unknown(7));
	}

	#[test]
	fn spot_shadows_can_be_turned_off()
	{
		let (level, _) = parse("lights = {\n\ta = {\n\t\ttype = $CLight.Spot\n\t\tshadows = $False\n\t}\n}\n");

		assert!(!level.lights[0].shadows);
	}

	#[test]
	fn a_sun_with_no_enabled_entry_is_on()
	{
		let (level, _) = parse("sun = {\n\tpos = [ 1, 2, 3 ]\n}\n");

		let sun = level.sun.unwrap();

		assert!(sun.enabled);
		assert_eq!(sun.position, [1.0, 2.0, 3.0]);
		assert!(sun.color.is_none() && sun.intensity.is_none());
	}

	#[test]
	fn a_camera_gives_only_the_values_it_sets()
	{
		let (level, _) = parse("camera = {\n\tiso = 400\n}\n");

		let camera = level.camera.unwrap();

		assert_eq!(camera.iso, Some(400.0));
		assert!(camera.aperture.is_none() && camera.shutter.is_none() && camera.exposure_ev.is_none());
	}

	#[test]
	fn a_file_with_a_syntax_error_reports_it()
	{
		let (level, _) = parse("all = {\n\ta = {\n\t\tpos = [ 1, \n");

		assert!(level.has_errors);
	}
}
