use raptor_entity::light_core::{NO_TILE, TYPE_DIRECTIONAL, TYPE_POINT, TYPE_SPOT};
use raptor_entity::{EntityCore, LightCore};
use raptor_math::frustum::ALL_PLANES;
use raptor_math::quat_platform as q;
use raptor_math::{Frustum, Mat4f, Vec3f};
use raptor_world::{Aabb as GridBox, WorldGrid};

use crate::light_gpu::{LightGpuData, ShadowInputs, light_gpu_data};
use crate::light_logic::{
	SpotShadowSettings, clamp_cone, intensity_from_lumens, spot_bounds, spot_shadow_matrix,
	spot_solid_angle,
};
use crate::shadow_atlas::ShadowAtlas;

pub const SPOT_SHADOW_NEAR_PLANE: f32 = 0.1;
const SPOT_SHADOW_FOV_PADDING: f32 = 1.0 * std::f32::consts::PI / 180.0;
const SPOT_SHADOW_MAX_HALF_FOV: f32 = 80.0 * std::f32::consts::PI / 180.0;
const MAX_CONE_ANGLE: f32 = 90.0 * std::f32::consts::PI / 180.0;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct LightId(pub u32);

impl LightId {
	pub const NULL: LightId = LightId(u32::MAX);
	pub const INVALID_BIT: u32 = 1 << 31;

	pub fn index(self) -> u32 {
		self.0 & !Self::INVALID_BIT
	}

	pub fn is_null(self) -> bool {
		self.0 == u32::MAX
	}

	pub fn is_invalid(self) -> bool {
		self.0 & Self::INVALID_BIT != 0
	}

	pub fn invalidated(self) -> LightId {
		LightId(self.0 | Self::INVALID_BIT)
	}
}

impl Default for LightId {
	fn default() -> Self {
		Self::NULL
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LightKind {
	Directional,
	Point,
	Spot,
}

impl LightKind {
	fn core_type(self) -> u32 {
		match self {
			LightKind::Directional => TYPE_DIRECTIONAL,
			LightKind::Point => TYPE_POINT,
			LightKind::Spot => TYPE_SPOT,
		}
	}
}

pub struct Light {
	pub name: String,
	pub entity: EntityCore,
	pub core: LightCore,
}

impl Light {
	pub fn new(name: &str, kind: LightKind) -> Self {
		let mut core = LightCore::default();
		core.light_type = kind.core_type();

		Self {
			name: name.to_owned(),
			entity: EntityCore::default(),
			core,
		}
	}

	pub fn point(name: &str) -> Self {
		Self::new(name, LightKind::Point)
	}

	pub fn directional(name: &str) -> Self {
		Self::new(name, LightKind::Directional)
	}

	pub fn spot(name: &str) -> Self {
		Self::new(name, LightKind::Spot)
	}

	pub fn kind(&self) -> LightKind {
		match self.core.light_type {
			TYPE_DIRECTIONAL => LightKind::Directional,
			TYPE_SPOT => LightKind::Spot,
			_ => LightKind::Point,
		}
	}

	pub fn id(&self) -> LightId {
		LightId(self.core.light_id)
	}

	pub fn is_enabled(&self) -> bool {
		self.core.enabled != 0
	}

	pub fn set_enabled(&mut self, enabled: bool) {
		self.core.enabled = u8::from(enabled);
	}

	pub fn casts_shadows(&self) -> bool {
		self.core.cast_shadows != 0
	}

	pub fn set_cast_shadows(&mut self, cast: bool) {
		self.core.cast_shadows = u8::from(cast);
	}

	pub fn is_cullable(&self) -> bool {
		self.core.is_cullable()
	}

	pub fn radius(&self) -> f32 {
		self.core.radius
	}

	pub fn position(&self) -> Vec3f {
		let [x, y, z, _] = self.entity.position;

		Vec3f::new(x, y, z)
	}

	pub fn rotation(&self) -> [f32; 4] {
		self.entity.rotation
	}

	pub fn bounds(&self) -> GridBox {
		let position = self.position();

		if self.kind() == LightKind::Spot {
			let (min, max) = spot_bounds(
				position,
				self.direction(),
				self.core.outer_angle,
				self.core.radius,
			);

			return GridBox {
				min: min.to_array(),
				max: max.to_array(),
			};
		}

		let extent = Vec3f::splat(self.core.radius);

		GridBox {
			min: (position - extent).to_array(),
			max: (position + extent).to_array(),
		}
	}

	pub fn is_outside_frustum(&self, frustum: &Frustum) -> bool {
		if self.kind() == LightKind::Spot {
			let bounds = self.bounds();

			return !frustum.intersects_aabb(
				&raptor_math::Aabb::new(
					Vec3f::from_array(bounds.min),
					Vec3f::from_array(bounds.max),
				),
				ALL_PLANES,
			);
		}

		if !self.is_cullable() {
			return false;
		}

		!frustum.intersects_sphere(self.position(), self.core.radius, ALL_PLANES)
	}

	fn update_grid(&self, grid: &mut WorldGrid) {
		grid.update_light(self.core.light_id, self.bounds(), self.is_cullable());
	}

	pub fn set_radius(&mut self, radius: f32, grid: &mut WorldGrid) {
		self.core.radius = radius;
		self.entity.set_scale(radius * 2.0);
		self.update_grid(grid);
	}

	pub fn set_position(&mut self, position: Vec3f, grid: &mut WorldGrid) {
		self.entity.set_position(position.to_array());
		self.update_grid(grid);
	}

	pub fn set_rotation(&mut self, rotation: [f32; 4], grid: &mut WorldGrid) {
		self.entity.set_rotation(rotation);
		self.update_grid(grid);
	}

	pub fn set_cone_angles(&mut self, inner: f32, outer: f32, grid: &mut WorldGrid) {
		let (inner, outer) = clamp_cone(inner, outer, MAX_CONE_ANGLE);

		self.core.inner_angle = inner;
		self.core.outer_angle = outer;
		self.update_grid(grid);
	}

	pub fn direction(&self) -> Vec3f {
		let [x, y, z, w] = self.entity.rotation;

		Vec3f(q::get_direction(q::set(x, y, z, w)))
	}

	pub fn set_direction(&mut self, direction: Vec3f, grid: &mut WorldGrid) {
		let dir = direction.normalize();
		let w = 1.0 + dir.z;

		let rotation = if w < 1e-6 {
			[0.0, 1.0, 0.0, 0.0]
		} else {
			q::get_values(q::normalize(q::set(-dir.y, dir.x, 0.0, w)))
		};

		self.set_rotation(rotation, grid);
	}

	pub fn effective_solid_angle(&self) -> f32 {
		spot_solid_angle(self.core.inner_angle, self.core.outer_angle)
	}

	pub fn lumens(&self) -> f32 {
		self.core.intensity * self.effective_solid_angle()
	}

	pub fn set_lumens(&mut self, lumens: f32) {
		self.core.intensity =
			intensity_from_lumens(lumens, self.core.inner_angle, self.core.outer_angle);
	}

	pub fn shadow_matrix(&self) -> Mat4f {
		spot_shadow_matrix(
			self.position(),
			self.direction(),
			self.core.outer_angle,
			self.core.radius,
			&SpotShadowSettings {
				fov_padding: SPOT_SHADOW_FOV_PADDING,
				max_half_fov: SPOT_SHADOW_MAX_HALF_FOV,
				near_plane: SPOT_SHADOW_NEAR_PLANE,
			},
		)
	}

	pub fn release_shadow_tile(&mut self, atlas: &mut ShadowAtlas) {
		if self.core.shadow_atlas_tile != NO_TILE {
			atlas.free_spot_tile(self.core.shadow_atlas_tile);
		}

		self.core.reset_shadow();
	}

	pub fn gpu_data(&mut self, shadow: &ShadowInputs) -> LightGpuData {
		self.entity.update_matrix_if_out_of_date();

		light_gpu_data(&self.core, &self.entity, shadow)
	}
}

#[derive(Default)]
pub struct LightList {
	lights: Vec<LightId>,
}

impl LightList {
	pub fn add(&mut self, id: LightId) {
		if id.is_null() || id.is_invalid() || self.contains(id) {
			return;
		}

		self.lights.push(id);
	}

	pub fn invalidate(&mut self, id: LightId) {
		if let Some(found) = self
			.lights
			.iter_mut()
			.find(|found| found.index() == id.index())
		{
			*found = found.invalidated();
		}
	}

	pub fn contains(&self, id: LightId) -> bool {
		self.lights
			.iter()
			.any(|found| !found.is_invalid() && found.index() == id.index())
	}

	pub fn clear(&mut self) {
		self.lights.clear();
	}

	pub fn len(&self) -> usize {
		self.lights.len()
	}

	pub fn is_empty(&self) -> bool {
		self.lights.is_empty()
	}

	pub fn lights(&self) -> &[LightId] {
		&self.lights
	}

	pub fn valid(&self) -> impl Iterator<Item = LightId> + '_ {
		self.lights.iter().copied().filter(|id| !id.is_invalid())
	}
}

pub struct LightStore {
	slots: Vec<Option<Light>>,
}

impl LightStore {
	pub fn new(capacity: usize) -> Self {
		Self {
			slots: (0..capacity).map(|_| None).collect(),
		}
	}

	pub fn capacity(&self) -> usize {
		self.slots.len()
	}

	pub fn add(&mut self, mut light: Light, grid: &mut WorldGrid) -> Option<LightId> {
		let index = self.slots.iter().position(Option::is_none)?;

		light.core.light_id = index as u32;
		light.entity.id = index as u32;

		grid.add_light(index as u32, light.bounds(), light.is_cullable());

		self.slots[index] = Some(light);

		Some(LightId(index as u32))
	}

	pub fn get(&self, id: LightId) -> Option<&Light> {
		if id.is_invalid() || id.is_null() {
			return None;
		}

		self.slots.get(id.index() as usize)?.as_ref()
	}

	pub fn get_mut(&mut self, id: LightId) -> Option<&mut Light> {
		if id.is_invalid() || id.is_null() {
			return None;
		}

		self.slots.get_mut(id.index() as usize)?.as_mut()
	}

	pub fn destroy(
		&mut self,
		id: LightId,
		grid: &mut WorldGrid,
		list: &mut LightList,
		atlas: &mut ShadowAtlas,
	) -> LightId {
		if id.is_invalid() || id.is_null() {
			return id;
		}

		if let Some(mut light) = self
			.slots
			.get_mut(id.index() as usize)
			.and_then(Option::take)
		{
			grid.remove_light(id.index());
			list.invalidate(id);
			light.release_shadow_tile(atlas);
		}

		id.invalidated()
	}

	pub fn find(&self, name: &str) -> Option<LightId> {
		self.iter()
			.find(|(_, light)| light.name == name)
			.map(|(id, _)| id)
	}

	pub fn directional(&self) -> Option<LightId> {
		self.iter()
			.find(|(_, light)| light.kind() == LightKind::Directional)
			.map(|(id, _)| id)
	}

	pub fn iter(&self) -> impl Iterator<Item = (LightId, &Light)> {
		self.slots
			.iter()
			.enumerate()
			.filter_map(|(index, slot)| slot.as_ref().map(|light| (LightId(index as u32), light)))
	}

	pub fn iter_mut(&mut self) -> impl Iterator<Item = (LightId, &mut Light)> {
		self.slots
			.iter_mut()
			.enumerate()
			.filter_map(|(index, slot)| slot.as_mut().map(|light| (LightId(index as u32), light)))
	}

	pub fn clear(&mut self, grid: &mut WorldGrid, list: &mut LightList, atlas: &mut ShadowAtlas) {
		let ids: Vec<_> = self.iter().map(|(id, _)| id).collect();

		for id in ids {
			self.destroy(id, grid, list, atlas);
		}
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	fn grid() -> WorldGrid {
		WorldGrid::new(Box::new(|_| {}))
	}

	#[test]
	fn ids_take_the_lowest_free_slot_and_are_found_by_name() {
		let mut grid = grid();
		let mut store = LightStore::new(4);
		let mut list = LightList::default();
		let mut atlas = ShadowAtlas::default();

		let a = store.add(Light::point("a"), &mut grid).unwrap();
		let b = store.add(Light::spot("b"), &mut grid).unwrap();

		assert_eq!((a.0, b.0), (0, 1));
		assert_eq!(store.find("b"), Some(b));

		let destroyed = store.destroy(a, &mut grid, &mut list, &mut atlas);

		assert!(destroyed.is_invalid());
		assert!(store.get(a).is_none());
		assert_eq!(store.add(Light::point("c"), &mut grid), Some(LightId(0)));
	}

	#[test]
	fn the_store_refuses_lights_when_it_is_full() {
		let mut grid = grid();
		let mut store = LightStore::new(1);

		assert!(store.add(Light::point("a"), &mut grid).is_some());
		assert!(store.add(Light::point("b"), &mut grid).is_none());
	}

	#[test]
	fn the_light_list_ignores_duplicates_and_skips_invalidated_lights() {
		let mut list = LightList::default();

		list.add(LightId(3));
		list.add(LightId(3));
		list.add(LightId::NULL);

		assert_eq!(list.len(), 1);

		list.invalidate(LightId(3));

		assert!(!list.contains(LightId(3)));
		assert_eq!(list.valid().count(), 0);
	}

	#[test]
	fn a_spot_light_points_along_its_direction() {
		let mut grid = grid();
		let mut light = Light::spot("s");

		light.set_direction(Vec3f::new(0.0, 0.0, 1.0), &mut grid);

		assert!((light.direction().z - 1.0).abs() < 1e-5);

		light.set_direction(Vec3f::new(1.0, 0.0, 0.0), &mut grid);

		assert!((light.direction().x - 1.0).abs() < 1e-4);
	}

	#[test]
	fn lumens_round_trip_through_intensity() {
		let mut light = Light::spot("s");

		light.set_lumens(500.0);

		assert!((light.lumens() - 500.0).abs() < 0.5);
	}

	#[test]
	fn setting_the_radius_scales_the_entity() {
		let mut grid = grid();
		let mut light = Light::point("p");

		light.set_radius(3.0, &mut grid);

		assert_eq!(light.entity.scale, 6.0);
		assert_eq!(light.bounds().max, [3.0, 3.0, 3.0]);
	}
}
