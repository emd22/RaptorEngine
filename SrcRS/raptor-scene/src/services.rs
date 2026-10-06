use raptor_anim::Skeleton;
use raptor_level::WeaponDef;
use raptor_weapon::WeaponScript;

use crate::blockout::BlockoutEnv;
use crate::scene::MeshRef;

/// One node of a model that has been loaded and uploaded, ready to become an object.
#[derive(Clone, Debug)]
pub struct LoadedNode {
	pub name: String,
	pub mesh: Option<MeshRef>,
	pub bounds: Option<([f32; 3], [f32; 3])>,
	pub material: u32,
	pub skeleton: Option<usize>,
	pub children: Vec<LoadedNode>,
}

pub struct LoadedModel {
	pub root: LoadedNode,
	pub skeletons: Vec<Skeleton>,
}

pub type ModelTicket = u64;

/// The parts of the engine that the world asks for things: loading models, drawing and decals.
pub trait Services: BlockoutEnv {
	fn request_model(&mut self, name: &str, path: &str) -> ModelTicket;

	fn poll_models(&mut self) -> Vec<(ModelTicket, Result<LoadedModel, String>)>;

	fn load_weapon_script(&mut self, def: &WeaponDef) -> Box<dyn WeaponScript>;

	fn add_bullet_hole(&mut self, point: [f32; 3], normal: [f32; 3]);

	fn add_blood_splat(&mut self, point: [f32; 3], normal: [f32; 3], size: f32);

	fn window_size(&self) -> [u32; 2];

	fn glyph_size(&self) -> [f32; 2];

	fn draw_text(&mut self, text: &str, position: [f32; 2], scale: f32, color: u32);

	fn random_unit(&mut self) -> f32;
}
