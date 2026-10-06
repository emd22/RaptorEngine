use std::sync::atomic::{AtomicBool, Ordering};

use crate::forward::material_features;

pub const FLAG_UNLIT: u32 = 1 << 0;
pub const FLAG_SPECULAR_GLOSSINESS: u32 = 1 << 1;
pub const FLAG_ALPHA_MASK: u32 = 1 << 2;
pub const FLAG_DOUBLE_SIDED: u32 = 1 << 3;

pub const TRANSPARENT_BELOW_ALPHA: f32 = 0.999;
pub const DEFAULT_QUALITY_LEVEL: i32 = 3;

/// Per-material constants, uploaded as they are to the material properties buffer. Mirrors
/// `Material` in `Shaders/MaterialDef.hlsli`.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct MaterialProperties
{
	pub flags: u32,
	pub alpha: f32,
	pub metallic_factor: f32,
	pub roughness_factor: f32,
	pub specular_factor: [f32; 3],
	pub glossiness_factor: f32,
	pub base_color_factor: [f32; 3],
	pub occlusion_strength: f32,
}

const _: () = assert!(size_of::<MaterialProperties>() == 48);

impl Default for MaterialProperties
{
	fn default() -> Self
	{
		Self {
			flags: 0,
			alpha: 1.0,
			metallic_factor: 0.0,
			roughness_factor: 0.5,
			specular_factor: [1.0; 3],
			glossiness_factor: 1.0,
			base_color_factor: [1.0; 3],
			occlusion_strength: 1.0,
		}
	}
}

/// One texture slot of a material, as far as readiness goes.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ComponentState
{
	pub exists: bool,
	pub has_image: bool,
	pub loaded: bool,
}

/// What the renderer knows about a material apart from its textures and descriptors: its
/// properties, and where it is in being built and made ready.
#[repr(C, align(16))]
#[derive(Debug)]
pub struct MaterialRecord
{
	pub properties: MaterialProperties,
	pub id: u32,
	pub quality_level: i32,
	pub supports_skinning: bool,
	pub nearest_filtering: bool,
	ready: bool,
	being_built: bool,
	requires_sync: bool,
	built: AtomicBool,
	ready_to_check: AtomicBool,
}

impl Default for MaterialRecord
{
	fn default() -> Self
	{
		Self {
			properties: MaterialProperties::default(),
			id: 0,
			quality_level: DEFAULT_QUALITY_LEVEL,
			supports_skinning: false,
			nearest_filtering: false,
			ready: false,
			being_built: false,
			requires_sync: true,
			built: AtomicBool::new(false),
			ready_to_check: AtomicBool::new(false),
		}
	}
}

impl MaterialRecord
{
	/// Back to what a new material starts as, with `id` as its place.
	pub fn reset(&mut self, id: u32)
	{
		*self = Self {
			id,
			..Self::default()
		};
	}

	pub fn copy_from(&mut self, other: &MaterialRecord)
	{
		self.id = other.id;
		self.properties = other.properties;
		self.supports_skinning = other.supports_skinning;
		self.nearest_filtering = other.nearest_filtering;
		self.ready = false;
		self.being_built = false;
		self.built.store(false, Ordering::Release);
	}

	pub fn is_built(&self) -> bool
	{
		self.built.load(Ordering::Acquire)
	}

	pub fn set_built(&self, built: bool)
	{
		self.built.store(built, Ordering::Release);
	}

	pub fn is_ready_to_check(&self) -> bool
	{
		self.ready_to_check.load(Ordering::Acquire)
	}

	pub fn set_ready_to_check(&self, value: bool)
	{
		self.ready_to_check.store(value, Ordering::Release);
	}

	pub fn requires_sync(&self) -> bool
	{
		self.requires_sync
	}

	pub fn mark_synced(&mut self)
	{
		self.requires_sync = false;
	}

	pub fn mark_requires_sync(&mut self)
	{
		self.requires_sync = true;
	}

	pub fn is_being_built(&self) -> bool
	{
		self.being_built
	}

	pub fn set_being_built(&mut self, value: bool)
	{
		self.being_built = value;
	}

	pub fn has_flag(&self, flag: u32) -> bool
	{
		self.properties.flags & flag != 0
	}

	fn set_flag(&mut self, flag: u32, value: bool)
	{
		if value {
			self.properties.flags |= flag;
		} else {
			self.properties.flags &= !flag;
		}

		self.requires_sync = true;
	}

	pub fn set_unlit(&mut self, value: bool)
	{
		self.set_flag(FLAG_UNLIT, value);
	}

	pub fn set_alpha_mask(&mut self, value: bool)
	{
		self.set_flag(FLAG_ALPHA_MASK, value);
	}

	pub fn set_double_sided(&mut self, value: bool)
	{
		self.set_flag(FLAG_DOUBLE_SIDED, value);
	}

	pub fn set_alpha(&mut self, alpha: f32)
	{
		self.properties.alpha = alpha;
		self.requires_sync = true;
	}

	pub fn set_metallic_roughness(&mut self, metallic: f32, roughness: f32)
	{
		self.properties.flags &= !FLAG_SPECULAR_GLOSSINESS;
		self.properties.metallic_factor = metallic;
		self.properties.roughness_factor = roughness;
		self.requires_sync = true;
	}

	pub fn set_specular_glossiness(&mut self, specular: [f32; 3], glossiness: f32)
	{
		self.properties.flags |= FLAG_SPECULAR_GLOSSINESS;
		self.properties.specular_factor = specular;
		self.properties.glossiness_factor = glossiness;
		self.requires_sync = true;
	}

	pub fn set_base_color_factor(&mut self, color: [f32; 3])
	{
		self.properties.base_color_factor = color;
		self.requires_sync = true;
	}

	pub fn set_occlusion_strength(&mut self, strength: f32)
	{
		self.properties.occlusion_strength = strength;
		self.requires_sync = true;
	}

	pub fn is_transparent(&self) -> bool
	{
		self.properties.alpha < TRANSPARENT_BELOW_ALPHA
	}

	/// What the material needs from the pipelines that draw it.
	pub fn pipeline_features(&self, has_normal_or_orm: bool) -> u32
	{
		material_features(
			has_normal_or_orm,
			self.supports_skinning,
			self.has_flag(FLAG_UNLIT),
		)
	}

	/// Whether the material can be drawn: it has been finalized and every texture it has is loaded.
	/// Once it is, it stays so until it is reset.
	pub fn evaluate_ready(&mut self, components: &[ComponentState]) -> bool
	{
		if !self.is_ready_to_check() {
			return false;
		}

		if self.ready {
			return true;
		}

		if components
			.iter()
			.any(|component| component.exists && !(component.has_image && component.loaded))
		{
			return false;
		}

		self.ready = true;

		true
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	const MISSING: ComponentState = ComponentState {
		exists: false,
		has_image: false,
		loaded: false,
	};

	const LOADED: ComponentState = ComponentState {
		exists: true,
		has_image: true,
		loaded: true,
	};

	#[test]
	fn a_new_material_has_the_default_properties_and_needs_a_sync()
	{
		let material = MaterialRecord::default();

		assert_eq!(material.properties, MaterialProperties::default());
		assert!(material.requires_sync());
		assert!(!material.is_built() && !material.is_ready_to_check());
		assert_eq!(material.quality_level, DEFAULT_QUALITY_LEVEL);
	}

	#[test]
	fn setters_change_the_properties_and_ask_for_a_sync()
	{
		let mut material = MaterialRecord::default();

		material.mark_synced();
		material.set_unlit(true);

		assert!(material.has_flag(FLAG_UNLIT) && material.requires_sync());

		material.mark_synced();
		material.set_alpha(0.5);

		assert!(material.is_transparent() && material.requires_sync());

		material.set_unlit(false);

		assert!(!material.has_flag(FLAG_UNLIT));
	}

	#[test]
	fn the_two_workflows_exclude_each_other()
	{
		let mut material = MaterialRecord::default();

		material.set_specular_glossiness([0.1, 0.2, 0.3], 0.7);

		assert!(material.has_flag(FLAG_SPECULAR_GLOSSINESS));
		assert_eq!(material.properties.specular_factor, [0.1, 0.2, 0.3]);

		material.set_metallic_roughness(0.9, 0.1);

		assert!(!material.has_flag(FLAG_SPECULAR_GLOSSINESS));
		assert_eq!(material.properties.metallic_factor, 0.9);
	}

	#[test]
	fn features_follow_the_flags_and_textures()
	{
		let mut material = MaterialRecord::default();

		assert_eq!(material.pipeline_features(false), 0);

		material.supports_skinning = true;
		material.set_unlit(true);

		assert_eq!(
			material.pipeline_features(true),
			material_features(true, true, true)
		);
	}

	#[test]
	fn readiness_waits_for_finalizing_and_for_every_texture()
	{
		let mut material = MaterialRecord::default();
		let pending = ComponentState {
			loaded: false,
			..LOADED
		};

		assert!(!material.evaluate_ready(&[LOADED]));

		material.set_ready_to_check(true);

		assert!(!material.evaluate_ready(&[LOADED, pending]));
		assert!(material.evaluate_ready(&[LOADED, MISSING, MISSING]));
		assert!(material.evaluate_ready(&[pending]));
	}

	#[test]
	fn copying_keeps_the_properties_but_not_the_progress()
	{
		let mut source = MaterialRecord::default();

		source.id = 7;
		source.set_alpha(0.25);
		source.nearest_filtering = true;
		source.set_ready_to_check(true);

		let mut copy = MaterialRecord::default();

		copy.copy_from(&source);

		assert_eq!(
			(copy.id, copy.properties.alpha, copy.nearest_filtering),
			(7, 0.25, true)
		);
		assert!(!copy.is_built());
		assert!(!copy.is_ready_to_check());
	}

	#[test]
	fn resetting_returns_to_a_fresh_material_at_a_new_id()
	{
		let mut material = MaterialRecord::default();

		material.set_alpha(0.1);
		material.set_ready_to_check(true);
		material.reset(5);

		assert_eq!(material.id, 5);
		assert_eq!(material.properties.alpha, 1.0);
		assert!(!material.is_ready_to_check());
	}
}
