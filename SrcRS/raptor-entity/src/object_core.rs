pub const NO_BONES: u32 = u32::MAX;
pub const NO_ID: u32 = u32::MAX;
pub const NO_BODY: u32 = u32::MAX;

pub const FLAG_DISABLE_CULLING: u16 = 1 << 5;
pub const FLAG_NOT_PROBE_VISIBLE: u16 = 1 << 6;
pub const FLAG_PHYSICS_ENABLED: u16 = 1 << 1;
pub const FLAG_IS_INSTANCE: u16 = 1 << 2;
pub const FLAG_SHADOW_CASTER: u16 = 1 << 3;
pub const FLAG_HAS_MESH: u16 = 1 << 8;
pub const FLAG_SKINNED: u16 = 1 << 9;

pub const TAG_PROBE_VOLUME: u32 = 1 << 2;

pub const LAYER_WORLD: u32 = 0;
pub const LAYER_PLAYER: u32 = 1;

/// What an object is and how it is drawn: its bounds, tags, flags and the ids that tie it to its
/// material, physics body, parent and bones. The engine reads this straight out of the record, so
/// the layout is fixed: it is also described in the C header.
#[repr(C, align(16))]
#[derive(Clone, Debug)]
pub struct ObjectCore
{
	pub bounds_min: [f32; 4],
	pub bounds_max: [f32; 4],
	pub tags: u32,
	pub material_id: u32,
	pub physics_id: u32,
	pub parent_id: u32,
	pub bone_buffer_base: u32,
	pub layer: u32,
	pub flags: u16,
	pub instance_slots: u16,
	pub instance_slots_in_use: u16,
	/// The ids of the nodes attached to this object. Only Rust reads this.
	children: Vec<u32>,
}

impl Default for ObjectCore
{
	fn default() -> Self
	{
		Self {
			bounds_min: [0.0; 4],
			bounds_max: [0.0; 4],
			tags: 0,
			material_id: 0,
			physics_id: NO_BODY,
			parent_id: NO_ID,
			bone_buffer_base: NO_BONES,
			layer: LAYER_WORLD,
			flags: 0,
			instance_slots: 0,
			instance_slots_in_use: 0,
			children: Vec::new(),
		}
	}
}

impl ObjectCore
{
	pub fn children(&self) -> &[u32]
	{
		&self.children
	}

	pub fn add_child(&mut self, id: u32)
	{
		self.children.push(id);
	}

	pub fn clear_children(&mut self)
	{
		self.children.clear();
	}

	pub fn has_flag(&self, flag: u16) -> bool
	{
		self.flags & flag != 0
	}

	pub fn set_flag(&mut self, flag: u16, value: bool)
	{
		if value {
			self.flags |= flag;
		} else {
			self.flags &= !flag;
		}
	}

	pub fn has_tag(&self, tag: u32) -> bool
	{
		self.tags & tag != 0
	}

	pub fn set_tag(&mut self, tag: u32, value: bool)
	{
		if value {
			self.tags |= tag;
		} else {
			self.tags &= !tag;
		}
	}

	/// Whether light probe bakes include the object. The view model is never part of the level.
	pub fn is_probe_visible(&self) -> bool
	{
		!self.has_flag(FLAG_NOT_PROBE_VISIBLE) && self.layer != LAYER_PLAYER
	}

	pub fn is_cullable(&self) -> bool
	{
		!self.has_flag(FLAG_DISABLE_CULLING)
	}

	pub fn has_bones_for_draw(&self, skinned: bool) -> bool
	{
		!skinned || self.bone_buffer_base != NO_BONES
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn layout_is_fixed()
	{
		assert_eq!(std::mem::offset_of!(ObjectCore, tags), 32);
		assert_eq!(std::mem::offset_of!(ObjectCore, layer), 52);
		assert_eq!(std::mem::offset_of!(ObjectCore, flags), 56);
		assert!(std::mem::size_of::<ObjectCore>() >= 64);
	}

	#[test]
	fn probe_visibility_follows_the_flag_and_the_layer()
	{
		let mut core = ObjectCore::default();

		assert!(core.is_probe_visible());

		core.set_flag(FLAG_NOT_PROBE_VISIBLE, true);

		assert!(!core.is_probe_visible());

		core.set_flag(FLAG_NOT_PROBE_VISIBLE, false);
		core.layer = LAYER_PLAYER;

		assert!(!core.is_probe_visible());
	}

	#[test]
	fn children_are_kept_in_the_order_they_were_attached()
	{
		let mut core = ObjectCore::default();

		core.add_child(4);
		core.add_child(2);

		assert_eq!(core.children(), &[4, 2]);

		core.clear_children();

		assert!(core.children().is_empty());
	}

	#[test]
	fn tags_set_and_clear_independently()
	{
		let mut core = ObjectCore::default();

		core.set_tag(0b101, true);
		core.set_tag(0b001, false);

		assert_eq!(core.tags, 0b100);
		assert!(core.has_tag(0b100));
	}
}
