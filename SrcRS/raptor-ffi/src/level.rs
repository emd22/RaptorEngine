use std::ffi::{CStr, CString, c_char};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::Path;

use raptor_level::{
	Block, BlockOut, BrushOut, BrushSource, CameraOut, Level, LevelOut, Light, LightKind,
	LightOut, PlaneOut, Rotation, SunOut, Texture,
};

use crate::config::{CHost, LogOnlyHost};
use crate::{RxHost, RxLogSink};

pub const BRUSH_INVALID: u32 = 0;
pub const BRUSH_BOX: u32 = 1;
pub const BRUSH_PLANES: u32 = 2;

pub const ROTATION_IDENTITY: u32 = 0;
pub const ROTATION_EULER: u32 = 1;
pub const ROTATION_QUAT: u32 = 2;

pub const LIGHT_POINT: i64 = 0;
pub const LIGHT_SPOT: i64 = 1;

#[repr(C)]
pub struct RxLevelSun
{
	pub present: u32,
	pub enabled: u32,
	pub has_color: u32,
	pub has_intensity: u32,
	pub position: [f32; 3],
	pub color: [i32; 3],
	pub intensity: f32,
}

#[repr(C)]
pub struct RxLevelLight
{
	pub name: *const c_char,
	pub name_length: usize,
	pub kind: i64,
	pub has_position: u32,
	pub has_color: u32,
	pub has_intensity: u32,
	pub has_direction: u32,
	pub has_rotation: u32,
	pub shadows: u32,
	pub position: [f32; 3],
	pub color: [i32; 3],
	pub intensity: f32,
	pub radius: f32,
	pub direction: [f32; 3],
	pub rotation: [f32; 4],
	pub inner_degrees: f32,
	pub outer_degrees: f32,
}

#[repr(C)]
pub struct RxLevelCamera
{
	pub present: u32,
	pub has_aperture: u32,
	pub has_shutter: u32,
	pub has_iso: u32,
	pub has_exposure_ev: u32,
	pub aperture: f32,
	pub shutter: f32,
	pub iso: f32,
	pub exposure_ev: f32,
}

#[repr(C)]
pub struct RxLevelPlane
{
	pub normal: [f32; 3],
	pub distance: f32,
	pub has_texture: u32,
	pub offset: [f32; 2],
	pub scale: [f32; 2],
	pub rotation: f32,
}

#[repr(C)]
pub struct RxLevelBlock
{
	pub name: *const c_char,
	pub name_length: usize,
	pub position: [f32; 3],
	pub brush_kind: u32,
	pub box_min: [f32; 3],
	pub box_max: [f32; 3],
	pub planes: *const RxLevelPlane,
	pub plane_count: u32,
	pub has_textures: u32,
	pub rotation_kind: u32,
	pub rotation: [f32; 4],
	pub locked: u32,
	pub has_material: u32,
	pub material: i32,
	pub probe_volume: u32,
	pub reflection_probe: u32,
	pub bleeds: u32,
	pub dynamic: u32,
}

pub struct RxLevel
{
	has_errors: bool,
	has_blocks: bool,
	sun: RxLevelSun,
	camera: RxLevelCamera,
	lights: Vec<RxLevelLight>,
	blocks: Vec<RxLevelBlock>,
	_names: Vec<CString>,
	_planes: Vec<Vec<RxLevelPlane>>,
}

fn c_name(name: &str, names: &mut Vec<CString>) -> (*const c_char, usize)
{
	let name = CString::new(name.replace('\0', "")).unwrap_or_default();
	let pointer = name.as_ptr();
	let length = name.as_bytes().len();

	names.push(name);

	(pointer, length)
}

fn light_to_c(light: &Light, names: &mut Vec<CString>) -> RxLevelLight
{
	let (name, name_length) = c_name(&light.name, names);

	RxLevelLight {
		name,
		name_length,
		kind: match light.kind {
			LightKind::Point => LIGHT_POINT,
			LightKind::Spot => LIGHT_SPOT,
			LightKind::Unknown(other) => other,
		},
		has_position: u32::from(light.position.is_some()),
		has_color: u32::from(light.color.is_some()),
		has_intensity: u32::from(light.intensity.is_some()),
		has_direction: u32::from(light.direction.is_some()),
		has_rotation: u32::from(light.rotation.is_some()),
		shadows: u32::from(light.shadows),
		position: light.position.unwrap_or_default(),
		color: light.color.unwrap_or_default(),
		intensity: light.intensity.unwrap_or_default(),
		radius: light.radius,
		direction: light.direction.unwrap_or_default(),
		rotation: light.rotation.unwrap_or_default(),
		inner_degrees: light.inner_degrees,
		outer_degrees: light.outer_degrees,
	}
}

fn block_to_c(
	block: &Block,
	names: &mut Vec<CString>,
	planes_store: &mut Vec<Vec<RxLevelPlane>>,
) -> RxLevelBlock
{
	let (name, name_length) = c_name(&block.name, names);

	let mut out = RxLevelBlock {
		name,
		name_length,
		position: block.position,
		brush_kind: BRUSH_INVALID,
		box_min: [0.0; 3],
		box_max: [0.0; 3],
		planes: std::ptr::null(),
		plane_count: 0,
		has_textures: 0,
		rotation_kind: ROTATION_IDENTITY,
		rotation: [0.0, 0.0, 0.0, 1.0],
		locked: u32::from(block.locked),
		has_material: u32::from(block.material.is_some()),
		material: block.material.unwrap_or_default(),
		probe_volume: u32::from(block.probe_volume),
		reflection_probe: u32::from(block.reflection_probe),
		bleeds: u32::from(block.bleeds),
		dynamic: u32::from(block.dynamic),
	};

	match block.rotation {
		Rotation::Identity => {}
		Rotation::Euler(angles) => {
			out.rotation_kind = ROTATION_EULER;
			out.rotation = [angles[0], angles[1], angles[2], 0.0];
		}
		Rotation::Quat(quat) => {
			out.rotation_kind = ROTATION_QUAT;
			out.rotation = quat;
		}
	}

	match &block.brush {
		BrushSource::Invalid => {}
		BrushSource::Box { min, max } => {
			out.brush_kind = BRUSH_BOX;
			out.box_min = *min;
			out.box_max = *max;
		}
		BrushSource::Planes {
			planes,
			has_textures,
		} => {
			let converted: Vec<RxLevelPlane> = planes
				.iter()
				.map(|plane| {
					let texture = plane.texture.unwrap_or(Texture {
						offset: [0.0; 2],
						scale: [0.0; 2],
						rotation: 0.0,
					});

					RxLevelPlane {
						normal: plane.normal,
						distance: plane.distance,
						has_texture: u32::from(plane.texture.is_some()),
						offset: texture.offset,
						scale: texture.scale,
						rotation: texture.rotation,
					}
				})
				.collect();

			out.brush_kind = BRUSH_PLANES;
			out.plane_count = converted.len() as u32;
			out.planes = converted.as_ptr();
			out.has_textures = u32::from(*has_textures);

			planes_store.push(converted);
		}
	}

	out
}

impl RxLevel
{
	fn new(level: &Level) -> Self
	{
		let mut names = Vec::new();
		let mut planes = Vec::new();

		let sun = match &level.sun {
			Some(sun) => RxLevelSun {
				present: 1,
				enabled: u32::from(sun.enabled),
				has_color: u32::from(sun.color.is_some()),
				has_intensity: u32::from(sun.intensity.is_some()),
				position: sun.position,
				color: sun.color.unwrap_or_default(),
				intensity: sun.intensity.unwrap_or_default(),
			},
			None => RxLevelSun {
				present: 0,
				enabled: 0,
				has_color: 0,
				has_intensity: 0,
				position: [0.0; 3],
				color: [0; 3],
				intensity: 0.0,
			},
		};

		let camera = match &level.camera {
			Some(camera) => RxLevelCamera {
				present: 1,
				has_aperture: u32::from(camera.aperture.is_some()),
				has_shutter: u32::from(camera.shutter.is_some()),
				has_iso: u32::from(camera.iso.is_some()),
				has_exposure_ev: u32::from(camera.exposure_ev.is_some()),
				aperture: camera.aperture.unwrap_or_default(),
				shutter: camera.shutter.unwrap_or_default(),
				iso: camera.iso.unwrap_or_default(),
				exposure_ev: camera.exposure_ev.unwrap_or_default(),
			},
			None => RxLevelCamera {
				present: 0,
				has_aperture: 0,
				has_shutter: 0,
				has_iso: 0,
				has_exposure_ev: 0,
				aperture: 0.0,
				shutter: 0.0,
				iso: 0.0,
				exposure_ev: 0.0,
			},
		};

		let lights = level
			.lights
			.iter()
			.map(|light| light_to_c(light, &mut names))
			.collect();

		let blocks = level
			.blocks
			.iter()
			.map(|block| block_to_c(block, &mut names, &mut planes))
			.collect();

		Self {
			has_errors: level.has_errors,
			has_blocks: level.has_blocks,
			sun,
			camera,
			lights,
			blocks,
			_names: names,
			_planes: planes,
		}
	}
}

/// Parses level text. `prelude_path` is the file of constants the level may refer to, or null.
///
/// # Safety
///
/// `data` must point at `length` readable bytes, `prelude_path` must be null or NUL-terminated,
/// and `host` must be valid. The result lasts until `rx_level_free`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_level_parse(
	data: *const u8,
	length: usize,
	prelude_path: *const c_char,
	host: *const RxHost,
) -> *mut RxLevel
{
	catch_unwind(AssertUnwindSafe(|| {
		let data = if data.is_null() {
			&[][..]
		}
		else {
			// SAFETY: guaranteed by the caller.
			unsafe { std::slice::from_raw_parts(data, length) }
		};

		let prelude = (!prelude_path.is_null()).then(|| {
			// SAFETY: NUL-terminated by the caller.
			unsafe { CStr::from_ptr(prelude_path) }.to_bytes()
		});

		// SAFETY: guaranteed by the caller.
		let mut host = CHost(unsafe { &*host });

		let level = Level::parse(data, prelude, &mut host);

		Box::into_raw(Box::new(RxLevel::new(&level)))
	}))
	.unwrap_or(std::ptr::null_mut())
}

/// # Safety
///
/// `level` must be null or come from `rx_level_parse`, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_level_free(level: *mut RxLevel)
{
	if !level.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(level) });
	}
}

/// # Safety
///
/// `level` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_level_has_errors(level: *const RxLevel) -> u8
{
	// SAFETY: guaranteed by the caller.
	u8::from(unsafe { &*level }.has_errors)
}

/// # Safety
///
/// `level` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_level_has_blocks(level: *const RxLevel) -> u8
{
	// SAFETY: guaranteed by the caller.
	u8::from(unsafe { &*level }.has_blocks)
}

/// # Safety
///
/// `level` must be live. The sun lasts as long as the level.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_level_sun(level: *const RxLevel) -> *const RxLevelSun
{
	// SAFETY: guaranteed by the caller.
	&raw const unsafe { &*level }.sun
}

/// # Safety
///
/// `level` must be live. The camera lasts as long as the level.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_level_camera(level: *const RxLevel) -> *const RxLevelCamera
{
	// SAFETY: guaranteed by the caller.
	&raw const unsafe { &*level }.camera
}

/// # Safety
///
/// `level` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_level_light_count(level: *const RxLevel) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*level }.lights.len() as u32
}

/// # Safety
///
/// `level` must be live and `index` in range. The light lasts as long as the level.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_level_light(level: *const RxLevel, index: u32) -> *const RxLevelLight
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*level }
		.lights
		.get(index as usize)
		.map_or(std::ptr::null(), std::ptr::from_ref)
}

/// # Safety
///
/// `level` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_level_block_count(level: *const RxLevel) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*level }.blocks.len() as u32
}

/// # Safety
///
/// `level` must be live and `index` in range. The block lasts as long as the level.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_level_block(level: *const RxLevel, index: u32) -> *const RxLevelBlock
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*level }
		.blocks
		.get(index as usize)
		.map_or(std::ptr::null(), std::ptr::from_ref)
}

#[repr(C)]
pub struct RxLevelWriteSun
{
	pub present: u32,
	pub enabled: u32,
	pub position: [f32; 3],
	pub color: [i32; 4],
	pub intensity: f32,
}

#[repr(C)]
pub struct RxLevelWriteLight
{
	pub name: *const c_char,
	pub name_length: usize,
	pub spot: u32,
	pub shadows: u32,
	pub position: [f32; 3],
	pub color: [i32; 4],
	pub intensity: f32,
	pub radius: f32,
	pub direction: [f32; 3],
	pub inner_degrees: f32,
	pub outer_degrees: f32,
}

#[repr(C)]
pub struct RxLevelWritePlane
{
	pub normal: [f32; 3],
	pub distance: f32,
	pub offset: [f32; 2],
	pub scale: [f32; 2],
	pub rotation: f32,
}

#[repr(C)]
pub struct RxLevelWriteBlock
{
	pub name: *const c_char,
	pub name_length: usize,
	pub position: [f32; 3],
	pub brush_kind: u32,
	pub box_extents: [f32; 6],
	pub planes: *const RxLevelWritePlane,
	pub plane_count: u32,
	pub textures: u32,
	pub rotation: [f32; 4],
	pub locked: u32,
	pub has_material: u32,
	pub material: i32,
	pub probe_volume: u32,
	pub reflection_probe: u32,
	pub bleeds: u32,
	pub dynamic: u32,
}

#[repr(C)]
pub struct RxLevelWrite
{
	pub sun: RxLevelWriteSun,
	pub lights: *const RxLevelWriteLight,
	pub light_count: u32,
	pub camera: [f32; 4],
	pub blocks: *const RxLevelWriteBlock,
	pub block_count: u32,
}

/// # Safety
///
/// `pointer` must be null or point at `count` items.
unsafe fn slice_or_empty<'a, T>(pointer: *const T, count: u32) -> &'a [T]
{
	if pointer.is_null() || count == 0 {
		return &[];
	}

	// SAFETY: guaranteed by the caller.
	unsafe { std::slice::from_raw_parts(pointer, count as usize) }
}

/// # Safety
///
/// `name` must point at `length` readable bytes.
unsafe fn name_from(name: *const c_char, length: usize) -> String
{
	if name.is_null() {
		return String::new();
	}

	// SAFETY: guaranteed by the caller.
	String::from_utf8_lossy(unsafe { std::slice::from_raw_parts(name.cast::<u8>(), length) })
		.into_owned()
}

/// # Safety
///
/// Every pointer in `write` must be valid for its count.
unsafe fn level_out_from(write: &RxLevelWrite) -> LevelOut
{
	// SAFETY: guaranteed by the caller.
	let (lights, blocks) = unsafe {
		(
			slice_or_empty(write.lights, write.light_count),
			slice_or_empty(write.blocks, write.block_count),
		)
	};

	LevelOut {
		sun: (write.sun.present != 0).then_some(SunOut {
			enabled: write.sun.enabled != 0,
			position: write.sun.position,
			color: write.sun.color,
			intensity: write.sun.intensity,
		}),
		lights: lights
			.iter()
			.map(|light| LightOut {
				// SAFETY: guaranteed by the caller.
				name: unsafe { name_from(light.name, light.name_length) },
				spot: light.spot != 0,
				position: light.position,
				color: light.color,
				intensity: light.intensity,
				radius: light.radius,
				direction: light.direction,
				inner_degrees: light.inner_degrees,
				outer_degrees: light.outer_degrees,
				shadows: light.shadows != 0,
			})
			.collect(),
		camera: CameraOut {
			aperture: write.camera[0],
			shutter: write.camera[1],
			iso: write.camera[2],
			exposure_ev: write.camera[3],
		},
		blocks: blocks
			.iter()
			.map(|block| BlockOut {
				// SAFETY: guaranteed by the caller.
				name: unsafe { name_from(block.name, block.name_length) },
				position: block.position,
				brush: if block.brush_kind == BRUSH_BOX {
					BrushOut::Box(block.box_extents)
				}
				else {
					BrushOut::Planes {
						// SAFETY: guaranteed by the caller.
						planes: unsafe { slice_or_empty(block.planes, block.plane_count) }
							.iter()
							.map(|plane| PlaneOut {
								normal: plane.normal,
								distance: plane.distance,
								texture: Texture {
									offset: plane.offset,
									scale: plane.scale,
									rotation: plane.rotation,
								},
							})
							.collect(),
						textures: block.textures != 0,
					}
				},
				rotation: block.rotation,
				locked: block.locked != 0,
				probe_volume: block.probe_volume != 0,
				reflection_probe: block.reflection_probe != 0,
				bleeds: block.bleeds != 0,
				dynamic: block.dynamic != 0,
				material: (block.has_material != 0).then_some(block.material),
			})
			.collect(),
	}
}

/// Writes a level file, replacing the old one only once the new one is whole. Returns 0 if it could
/// not be written.
///
/// # Safety
///
/// `path` must be NUL-terminated, `write` and everything it points to valid, and `log` valid.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_level_save(
	path: *const c_char,
	write: *const RxLevelWrite,
	log: *const RxLogSink,
) -> u8
{
	catch_unwind(AssertUnwindSafe(|| {
		// SAFETY: guaranteed by the caller.
		let (path, out, mut host) = unsafe {
			(
				CStr::from_ptr(path).to_string_lossy().into_owned(),
				level_out_from(&*write),
				LogOnlyHost(&*log),
			)
		};

		let text = out.to_text(&mut host);

		u8::from(raptor_level::save::write_atomic(Path::new(&path), &text).is_ok())
	}))
	.unwrap_or(0)
}

const _: () = {
	use std::mem::size_of;

	assert!(size_of::<RxLevelSun>() == 44);
	assert!(size_of::<RxLevelLight>() == 120);
	assert!(size_of::<RxLevelCamera>() == 36);
	assert!(size_of::<RxLevelPlane>() == 40);
	assert!(size_of::<RxLevelBlock>() == 120);
	assert!(size_of::<RxLevelWriteSun>() == 40);
	assert!(size_of::<RxLevelWriteLight>() == 80);
	assert!(size_of::<RxLevelWritePlane>() == 36);
	assert!(size_of::<RxLevelWriteBlock>() == 120);
	assert!(size_of::<RxLevelWrite>() == 88);
};
