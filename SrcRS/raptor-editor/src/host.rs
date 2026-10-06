use raptor_brush::Plane;
use raptor_math::mat4::Quat;
use raptor_math::{Aabb, Mat4f, Vec3f};

use crate::key::Key;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct ObjectId(pub u32);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct LightId(pub u32);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct MaterialId(pub u32);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct BodyId(pub u32);

pub type Rgba = [u8; 4];

pub const TAG_BLOCKOUT: u32 = 1 << 0;
pub const TAG_LOCK_TRANSFORM: u32 = 1 << 1;
pub const TAG_PROBE_VOLUME: u32 = 1 << 2;
pub const TAG_REFLECTION_PROBE: u32 = 1 << 3;
pub const TAG_BLEEDS: u32 = 1 << 4;

pub const FLAG_READY_TO_RENDER: u32 = 1 << 0;
pub const FLAG_PHYSICS_ENABLED: u32 = 1 << 1;
pub const FLAG_IS_INSTANCE: u32 = 1 << 2;
pub const FLAG_SHADOW_CASTER: u32 = 1 << 3;
pub const FLAG_UNLIT: u32 = 1 << 4;
pub const FLAG_DISABLE_CULLING: u32 = 1 << 5;
pub const FLAG_NOT_PROBE_VISIBLE: u32 = 1 << 6;

pub const DEBUG_BOUNDS_OBJECTS: u32 = 1 << 0;
pub const DEBUG_BOUNDS_LIGHTS: u32 = 1 << 1;
pub const DEBUG_BOUNDS_PHYSICS: u32 = 1 << 2;

#[derive(Clone, Copy, Debug)]
pub struct RayHit {
	pub point: Vec3f,
	pub normal: Vec3f,
	pub body: Option<BodyId>,
}

#[derive(Clone, Debug)]
pub struct SpotLight {
	pub name: String,
	pub position: Vec3f,
	pub direction: Vec3f,
	pub radius: f32,
	pub inner_angle: f32,
	pub outer_angle: f32,
	pub cast_shadows: bool,
	pub color: Rgba,
	pub intensity: f32,
}

impl Default for SpotLight {
	fn default() -> Self {
		Self {
			name: String::new(),
			position: Vec3f::ZERO,
			direction: Vec3f::FORWARD,
			radius: 5.0,
			inner_angle: 0.0,
			outer_angle: 0.0,
			cast_shadows: true,
			color: [255, 255, 255, 255],
			intensity: 100_000.0,
		}
	}
}

#[derive(Clone, Copy, Debug, Default)]
pub struct ProbeStatus {
	pub reflection_probes: u32,
	pub baking_reflections: bool,
	pub baking_irradiance: bool,
	pub reflections_baked: bool,
}

#[derive(Clone, Debug)]
pub struct CvarInfo {
	pub name: String,
	pub type_name: &'static str,
	pub value: String,
}

#[derive(Clone, Debug)]
pub struct BlockSnapshot {
	pub name: String,
	pub position: Vec3f,
	pub rotation: Quat,
	pub material: MaterialId,
	pub is_probe_volume: bool,
	pub is_reflection_probe: bool,
	pub is_dynamic: bool,
}

pub trait InputHost {
	fn key_down(&self, key: Key) -> bool;
	fn key_pressed(&self, key: Key) -> bool;

	fn combo_pressed(&self, held: Key, pressed: Key) -> bool {
		self.key_down(held)
			&& self.key_down(pressed)
			&& (self.key_pressed(held) || self.key_pressed(pressed))
	}
}

pub trait PlayerHost {
	fn camera_position(&self) -> Vec3f;
	fn camera_forward(&self) -> Vec3f;
	fn player_position(&self) -> Vec3f;
	fn teleport_player(&mut self, position: Vec3f);
	fn set_player_speed_multiplier(&mut self, multiplier: f32);
}

pub trait CvarHost {
	fn cvar_int(&self, name: &str, default: i64) -> i64;
	fn cvar_float(&self, name: &str, default: f32) -> f32;
	fn set_cvar_int(&mut self, name: &str, value: i64);
	fn set_cvar_float(&mut self, name: &str, value: f32);
	fn cvars(&self) -> Vec<CvarInfo>;
	fn set_cvar_from_str(&mut self, name: &str, value: &str) -> bool;

	fn cvar_bool(&self, name: &str, default: bool) -> bool {
		self.cvar_int(name, default as i64) != 0
	}

	fn set_cvar_bool(&mut self, name: &str, value: bool) {
		self.set_cvar_int(name, value as i64);
	}
}

pub trait PhysicsHost {
	fn raycast(&self, origin: Vec3f, delta: Vec3f) -> Option<RayHit>;
	fn raycast_objects(&self, origin: Vec3f, delta: Vec3f) -> Vec<ObjectId>;
	fn body_is_dynamic(&self, body: BodyId) -> bool;
	fn body_point_to_local(&self, body: BodyId, point: Vec3f) -> Vec3f;
	fn body_hold(
		&mut self,
		body: BodyId,
		local_point: Vec3f,
		target: Vec3f,
		stiffness: f32,
		max_speed: f32,
		angular_damping: f32,
	) -> Option<Vec3f>;
}

pub trait ObjectHost {
	fn object_exists(&self, id: ObjectId) -> bool;
	fn object_ids(&self) -> Vec<ObjectId>;
	fn object_name(&self, id: ObjectId) -> String;
	fn object_position(&self, id: ObjectId) -> Vec3f;
	fn set_object_position(&mut self, id: ObjectId, position: Vec3f);
	fn object_rotation(&self, id: ObjectId) -> Quat;
	fn object_euler(&self, id: ObjectId) -> Vec3f;
	fn set_object_euler(&mut self, id: ObjectId, euler: Vec3f);
	fn object_bounds(&self, id: ObjectId) -> Aabb;
	fn set_object_bounds(&mut self, id: ObjectId, bounds: Aabb);
	fn object_tags(&self, id: ObjectId) -> u32;
	fn object_flags(&self, id: ObjectId) -> u32;
	fn set_object_tag(&mut self, id: ObjectId, bit: u32, enabled: bool);
	fn set_object_flag(&mut self, id: ObjectId, bit: u32, enabled: bool);
	fn object_children(&self, id: ObjectId) -> Vec<ObjectId>;
	fn object_parent(&self, id: ObjectId) -> Option<ObjectId>;
	fn object_has_mesh(&self, id: ObjectId) -> bool;
	fn object_is_player_layer(&self, id: ObjectId) -> bool;
	fn object_in_world(&self, id: ObjectId) -> bool;
	fn object_world_matrix(&self, id: ObjectId) -> Mat4f;
	fn object_raycast_bounds(
		&self,
		id: ObjectId,
		origin: Vec3f,
		direction: Vec3f,
	) -> Option<(f32, Vec3f)>;
	fn object_contains_point(&self, id: ObjectId, point: Vec3f) -> bool;
	fn object_material(&self, id: ObjectId) -> MaterialId;
	fn set_object_material(&mut self, id: ObjectId, material: MaterialId);
	fn object_has_dynamic_body(&self, id: ObjectId) -> bool;
	fn raycast_probe_volumes(
		&self,
		origin: Vec3f,
		direction: Vec3f,
		range: f32,
	) -> Option<(ObjectId, f32)>;
}

pub trait BlockoutHost {
	fn has_blockout(&self) -> bool;
	fn brush_planes(&self, id: ObjectId) -> Option<Vec<Plane>>;
	fn set_brush_planes(&mut self, id: ObjectId, planes: &[Plane]);
	fn rebuild_block(&mut self, id: ObjectId);
	fn new_block(&mut self, position: Vec3f) -> Option<ObjectId>;
	fn dupe_block(&mut self, id: ObjectId) -> Option<ObjectId>;
	fn restore_block(&mut self, snapshot: &BlockSnapshot, planes: &[Plane]) -> Option<ObjectId>;
	fn destroy_block(&mut self, id: ObjectId);
	fn block_is_dynamic(&self, id: ObjectId) -> bool;
	fn default_material(&self) -> MaterialId;
	fn selection_material(&self) -> MaterialId;
	fn materials(&self) -> Vec<(MaterialId, String)>;
	fn show_preview(&mut self, position: Vec3f, rotation: Quat, planes: &[Plane]);
	fn hide_preview(&mut self);
	fn set_transform_marker(&mut self, position: Option<Vec3f>);
	fn blockout_path(&self) -> Option<String>;
	fn set_blockout_path(&mut self, path: &str);
	fn save_blockout(&mut self, path: &str) -> bool;
	fn load_blockout(&mut self, path: &str) -> bool;
}

pub trait LightHost {
	fn spot_lights(&self) -> Vec<LightId>;
	fn spot_light(&self, id: LightId) -> Option<SpotLight>;
	fn set_light_position(&mut self, id: LightId, position: Vec3f);
	fn set_light_direction(&mut self, id: LightId, direction: Vec3f);
	fn set_light_radius(&mut self, id: LightId, radius: f32);
	fn set_light_cone(&mut self, id: LightId, inner: f32, outer: f32);
	fn set_light_color(&mut self, id: LightId, color: Rgba);
	fn set_light_intensity(&mut self, id: LightId, intensity: f32);
	fn light_lumens(&self, id: LightId) -> f32;
	fn set_light_lumens(&mut self, id: LightId, lumens: f32);
	fn create_spot_light(&mut self, light: &SpotLight) -> Option<LightId>;
	fn destroy_light(&mut self, id: LightId);
	fn light_count(&self) -> u32;
	fn light_name_in_use(&self, name: &str) -> bool;
}

pub trait ProbeHost {
	fn rebuild_reflection_probes(&mut self);
	fn probe_status(&self) -> ProbeStatus;
	fn bake_reflections(&mut self);
	fn bake_irradiance(&mut self);
	fn rebuild_probe_volumes(&mut self) -> u32;
	fn probe_volume_count(&self) -> u32;
	fn probe_count(&self) -> u32;
	fn is_baking(&self) -> bool;
	fn save_probes(&mut self) -> bool;
	fn load_probes(&mut self);
}

pub trait DrawHost {
	fn debug_line(&mut self, from: Vec3f, to: Vec3f, color: Rgba);
	fn debug_solid_box(&mut self, center: Vec3f, half_extent: Vec3f, color: Rgba);
	fn debug_wire_box(&mut self, transform: &Mat4f, color: Rgba);
}

pub trait EditorHost:
	InputHost
	+ PlayerHost
	+ CvarHost
	+ PhysicsHost
	+ ObjectHost
	+ BlockoutHost
	+ LightHost
	+ ProbeHost
	+ DrawHost
{
}

impl<T> EditorHost for T where
	T: InputHost
		+ PlayerHost
		+ CvarHost
		+ PhysicsHost
		+ ObjectHost
		+ BlockoutHost
		+ LightHost
		+ ProbeHost
		+ DrawHost
{
}
