use std::collections::HashMap;
use std::path::PathBuf;

use raptor_anim::Skeleton;
use raptor_anim::math::Mat4;
use raptor_input::Controls;
use raptor_jolt::JoltBackend;
use raptor_level::{Collider as ColliderDef, ObjectDef, WeaponDef};
use raptor_math::Mat4f;
use raptor_physics::ragdoll::{CreateOptions, Log as RagdollLog, Ragdoll};
use raptor_physics::{Backend, BodyId, BodyProps, Motion, ragdoll_user_data};
use raptor_weapon::{
	CameraBasis, Event, InputFlags, SurfaceHit, WeaponEnv, WeaponScript, WeaponSystem,
};

use crate::blockout::{Blockout, TAG_BLEEDS};
use crate::player::{
	DEFAULT_VIEW_KICKBACK, PLAYER_CONFIG_PATH, Player, PlayerConfig, ViewModelRig,
};
use crate::scene::{ObjectId, Scene, SkeletonRef};
use crate::services::{LoadedModel, LoadedNode, ModelTicket, Services};

pub const VIEW_MODEL_PATH: &str = "RaptorData/Data/Demo/Models/viewmodel/viewmodel_baked.glb";
pub const RAGDOLL_TEMPLATE_PATH: &str = "RaptorData/Data/Demo/Models/ragdoll/ragdoll.glb";
pub const LAYER_PLAYER: u32 = 1;

pub const DEBUG_OBJECTS: u32 = 1 << 0;
pub const DEBUG_LIGHTS: u32 = 1 << 1;
pub const DEBUG_PHYSICS: u32 = 1 << 2;

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum DebugShape {
	WireAabb {
		min: [f32; 3],
		max: [f32; 3],
		color: [u8; 4],
	},
	WireBox {
		transform: Mat4f,
		color: [u8; 4],
	},
}

enum Pending {
	ViewModel,
	RagdollTemplate,
	SceneObject(Box<ObjectDef>),
}

struct Dummy {
	root: ObjectId,
	skinned: ObjectId,
	ragdoll: Ragdoll,
}

struct LogSink;

impl RagdollLog for LogSink {
	fn info(&mut self, message: &str) {
		raptor_core::log_info!("{message}");
	}

	fn warn(&mut self, message: &str) {
		raptor_core::log_warn!("{message}");
	}
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RagdollImpactInfo {
	pub serial: u32,
	pub point: [f32; 3],
	pub normal: [f32; 3],
	pub speed: f32,
}

pub struct World<B: Backend = JoltBackend> {
	pub scene: Scene<B>,
	pub blockout: Blockout,
	pub player: Player,
	pub weapons: WeaponSystem<Box<dyn WeaponScript>>,
	pub controls: Controls,
	pub services: Box<dyn Services>,
	pub name: String,
	pub blockout_path: String,
	pub scene_path: String,
	pub populated: bool,
	pub debug_bounds_mask: u32,
	pub render_probes: bool,
	base: PathBuf,
	skeletons: Vec<Option<Skeleton>>,
	pending: HashMap<ModelTicket, Pending>,
	dummies: HashMap<u32, Dummy>,
	next_dummy: u32,
	next_serial: u32,
	view_model: Option<ObjectId>,
	view_model_skeleton: Option<SkeletonRef>,
	ragdoll_template: Option<ObjectId>,
}

struct Env<'a, B: Backend> {
	player: &'a mut Player,
	scene: &'a mut Scene<B>,
	services: &'a mut dyn Services,
	skeleton: Option<&'a mut Skeleton>,
}

fn cross(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
	[
		a[1] * b[2] - a[2] * b[1],
		a[2] * b[0] - a[0] * b[2],
		a[0] * b[1] - a[1] * b[0],
	]
}

fn normalized(a: [f32; 3]) -> [f32; 3] {
	let length = (a[0] * a[0] + a[1] * a[1] + a[2] * a[2]).sqrt();

	if length <= f32::EPSILON {
		return [0.0; 3];
	}

	[a[0] / length, a[1] / length, a[2] / length]
}

pub fn camera_basis(position: [f32; 3], forward: [f32; 3]) -> CameraBasis {
	let right = normalized(cross([0.0, 1.0, 0.0], forward));
	let up = normalized(cross(forward, right));

	CameraBasis {
		position,
		forward,
		right,
		up,
	}
}

impl<B: Backend> WeaponEnv for Env<'_, B> {
	fn camera(&self) -> CameraBasis {
		let camera = &self.player.camera;

		camera_basis(
			[camera.position[0], camera.position[1], camera.position[2]],
			[
				camera.direction[0],
				camera.direction[1],
				camera.direction[2],
			],
		)
	}

	fn player_velocity(&self) -> [f32; 3] {
		self.player.linear_velocity(&self.scene.physics)
	}

	fn player_grounded(&self) -> bool {
		self.player.is_grounded()
	}

	fn player_fly_mode(&self) -> bool {
		self.player.is_fly_mode()
	}

	fn set_view_model_animations(&mut self, idle: &str, fire: &str, reload: &str) {
		let rig = self
			.skeleton
			.as_deref_mut()
			.map(|skeleton| skeleton as &mut dyn ViewModelRig);

		self.player
			.set_view_model_animations(rig, idle, fire, reload);
	}

	fn set_recoil_recovery(&mut self, rate_per_second: f32) {
		self.player.set_recoil_recovery(rate_per_second);
	}

	fn set_view_model_holster(&mut self, amount: f32) {
		self.player.set_view_model_holster(amount);
	}

	fn do_fire_animation(&mut self, kick_degrees: f32) {
		let yaw = self.services.random_unit() * 2.0 - 1.0;
		let roll = self.services.random_unit() * 2.0 - 1.0;

		let rig = self
			.skeleton
			.as_deref_mut()
			.map(|skeleton| skeleton as &mut dyn ViewModelRig);

		self.player
			.do_fire_animation(rig, kick_degrees, DEFAULT_VIEW_KICKBACK, yaw, roll);
	}

	fn do_reload_animation(&mut self) {
		let rig = self
			.skeleton
			.as_deref_mut()
			.map(|skeleton| skeleton as &mut dyn ViewModelRig);

		self.player.do_reload_animation(rig);
	}

	fn add_recoil(&mut self, pitch: f32, yaw: f32) {
		self.player.add_recoil(pitch, yaw);
	}

	fn raycast(
		&self,
		origin: [f32; 3],
		direction: [f32; 3],
		ignore: Option<u32>,
	) -> Option<SurfaceHit> {
		let hit = self
			.scene
			.physics
			.raycast(origin, direction, ignore.map(BodyId))?;

		Some(SurfaceHit {
			body: hit.body.0,
			point: hit.point,
			normal: hit.normal,
		})
	}

	fn body_is_dynamic(&self, body: u32) -> bool {
		self.scene.physics.is_dynamic(BodyId(body))
	}

	fn body_bleeds(&self, body: u32) -> bool {
		self.scene
			.colliders
			.find_by_body(BodyId(body))
			.and_then(|collider| self.scene.colliders.get(collider))
			.and_then(|collider| collider.object)
			.and_then(|object| self.scene.core(object))
			.is_some_and(|core| core.has_tag(TAG_BLEEDS))
	}

	fn push_body(&mut self, body: u32, impulse: [f32; 3]) {
		self.scene.physics.push(BodyId(body), impulse);
	}

	fn add_bullet_hole(&mut self, point: [f32; 3], normal: [f32; 3]) {
		self.services.add_bullet_hole(point, normal);
	}

	fn add_blood_splat(&mut self, point: [f32; 3], normal: [f32; 3], size: f32) {
		self.services.add_blood_splat(point, normal, size);
	}

	fn random_unit(&mut self) -> f32 {
		self.services.random_unit()
	}

	fn window_size(&self) -> [u32; 2] {
		self.services.window_size()
	}

	fn glyph_size(&self) -> [f32; 2] {
		self.services.glyph_size()
	}

	fn draw_text(&mut self, text: &str, position: [f32; 2], scale: f32, color: u32) {
		self.services.draw_text(text, position, scale, color);
	}

	fn debug_enabled(&self) -> bool {
		raptor_core::cvar::bool("b_weapon_debug", false)
	}

	fn log(&mut self, message: &str) {
		raptor_core::log_info!("{message}");
	}
}

impl<B: Backend> World<B> {
	pub fn new(backend: B, services: Box<dyn Services>, base: PathBuf) -> Self {
		let _ = raptor_core::cvar::set_bool("b_weapon_debug", false);
		let _ = raptor_core::cvar::set_int("r_show_volumes", 0);
		let _ = raptor_core::cvar::set_int("r_frustum_cull", 1);

		Self {
			scene: Scene::new(backend),
			blockout: Blockout::new(),
			player: Player::new(),
			weapons: WeaponSystem::new(),
			controls: Controls::new(),
			services,
			name: "(unnamed)".to_owned(),
			blockout_path: String::new(),
			scene_path: String::new(),
			populated: false,
			debug_bounds_mask: 0,
			render_probes: false,
			base,
			skeletons: Vec::new(),
			pending: HashMap::new(),
			dummies: HashMap::new(),
			next_dummy: 0,
			next_serial: 0,
			view_model: None,
			view_model_skeleton: None,
			ragdoll_template: None,
		}
	}

	pub fn base(&self) -> &std::path::Path {
		&self.base
	}

	pub fn skeleton(&self, skeleton: SkeletonRef) -> Option<&Skeleton> {
		self.skeletons.get(skeleton.0 as usize)?.as_ref()
	}

	pub fn skeleton_mut(&mut self, skeleton: SkeletonRef) -> Option<&mut Skeleton> {
		self.skeletons.get_mut(skeleton.0 as usize)?.as_mut()
	}

	fn store_skeleton(&mut self, skeleton: Skeleton) -> SkeletonRef {
		store(&mut self.skeletons, skeleton)
	}

	pub fn init_grid(&mut self, size: [u32; 2]) {
		self.scene.grid.create(size);
	}

	pub fn create_blockout(&mut self, path: Option<&str>) {
		self.blockout.create(&mut self.scene, &mut *self.services);

		if let Some(path) = path {
			self.blockout_path = path.to_owned();

			self.reload_blockout();
		}
	}

	pub fn reload_blockout(&mut self) -> bool {
		let path = self.blockout_path.clone();

		self.blockout
			.load(&mut self.scene, &mut *self.services, &self.base, &path)
	}

	pub fn save_blockout(&mut self, path: &str) -> bool {
		self.blockout
			.save(&mut self.scene, &mut *self.services, &self.base, path)
	}

	pub fn begin_game(&mut self, aspect_ratio: f32) {
		let config = match std::fs::read(self.base.join(PLAYER_CONFIG_PATH)) {
			Ok(bytes) => PlayerConfig::parse(&bytes, None),
			Err(_) => {
				raptor_core::log_warn!("Could not read {PLAYER_CONFIG_PATH}");
				PlayerConfig::default()
			}
		};

		self.player
			.create(&mut self.scene.physics, &config, aspect_ratio);
		self.player
			.teleport_to(&mut self.scene.physics, [0.0, -0.2, -2.0]);
		self.set_fly_mode(false);

		let ticket = self.services.request_model("view_model", VIEW_MODEL_PATH);

		self.pending.insert(ticket, Pending::ViewModel);

		self.install_weapons();

		self.scene.physics.optimize();
	}

	fn install_weapons(&mut self) {
		let defs: Vec<WeaponDef> = WeaponSystem::<Box<dyn WeaponScript>>::discover(&self.base);

		let weapons: Vec<(WeaponDef, Box<dyn WeaponScript>)> = defs
			.into_iter()
			.map(|def| {
				let script = self.services.load_weapon_script(&def);

				(def, script)
			})
			.collect();

		let skeleton = self
			.view_model_skeleton
			.and_then(|skeleton| self.skeletons.get_mut(skeleton.0 as usize))
			.and_then(Option::as_mut);

		let mut env = Env {
			player: &mut self.player,
			scene: &mut self.scene,
			services: &mut *self.services,
			skeleton,
		};

		self.weapons.install(&mut env, weapons);
	}

	pub fn request_scene_object(&mut self, model_path: &str, object: &ObjectDef) {
		let ticket = self.services.request_model(&object.name, model_path);

		self.pending
			.insert(ticket, Pending::SceneObject(Box::new(object.clone())));
	}

	pub fn request_ragdoll_template(&mut self) {
		let ticket = self
			.services
			.request_model("ragdoll_template", RAGDOLL_TEMPLATE_PATH);

		self.pending.insert(ticket, Pending::RagdollTemplate);
	}

	pub fn ragdoll_template_ready(&self) -> bool {
		self.ragdoll_template.is_some()
	}

	pub fn add_collider(&mut self, def: &ColliderDef) {
		let Some(collider) = self.scene.colliders.create(&def.name) else {
			raptor_core::log_error!("No room for collider '{}'", def.name);
			return;
		};

		let motion = if def.dynamic {
			Motion::Dynamic
		} else {
			Motion::Static
		};

		if let Some(mut size) = def.box_size {
			if size.iter().any(|value| *value <= 0.0) {
				raptor_core::log_warn!(
					"Collider '{}' has invalid Size {size:?} - falling back to 1,1,1",
					def.name
				);
				size = [1.0; 3];
			}

			if let Err(error) = self.scene.colliders.create_box(
				collider,
				&mut self.scene.physics,
				size,
				motion,
				&BodyProps::default(),
			) {
				raptor_core::log_error!("Failed to create collider '{}': {error:?}", def.name);
			}
		}

		self.scene.colliders.teleport(
			collider,
			&mut self.scene.physics,
			def.position,
			def.rotation,
		);
	}

	pub fn apply_object_def(&mut self, object: ObjectId, def: &ObjectDef) {
		if let Some(shadows) = def.shadows {
			self.scene.set_shadow_caster(object, shadows);
		}

		let position = def.position.or_else(|| self.scene.position(object));
		let rotation = def.rotation.or_else(|| self.scene.rotation(object));
		let scale = def.scale.or_else(|| self.scene.scale(object));

		if let Some(position) = position {
			self.scene.set_position(object, position);
		}

		if let Some(rotation) = rotation {
			self.scene.set_rotation(object, rotation);
		}

		if let Some(scale) = scale {
			self.scene.set_scale(object, scale);
		}

		if let Some(entity) = self.scene.entity_mut(object) {
			entity.mark_transform_out_of_date();
		}

		if def.layer == Some(i64::from(LAYER_PLAYER)) {
			self.scene.set_object_layer(object, LAYER_PLAYER);
		}

		self.scene.set_unlit(object, def.unlit);

		if def.no_cull {
			self.scene.set_cullable(object, false);
		}

		if let Some(name) = &def.collider {
			match self
				.scene
				.colliders
				.find_by_name_hash(crate::colliders::hash_name(name))
			{
				Some(collider) => {
					self.scene.attach_collider(object, collider);
					self.scene.set_physics_enabled(object, true);
				}
				None => {
					if let Some(core) = self.scene.core_mut(object) {
						core.physics_id = raptor_entity::object_core::NO_BODY;
					}
				}
			}
		}
	}

	pub fn update_object_def(&mut self, def: &ObjectDef) -> bool {
		let Some(object) = self.scene.find_by_name(&def.name) else {
			return false;
		};

		self.apply_object_def(object, def);

		true
	}

	fn instantiate_node(
		&mut self,
		node: &LoadedNode,
		skeletons: &[SkeletonRef],
	) -> Option<ObjectId> {
		let id = self.scene.new_object(&node.name, node.material, 0)?;

		self.scene.set_mesh(id, node.mesh);

		if let Some((min, max)) = node.bounds {
			self.scene.set_bounds(
				id,
				raptor_math::Aabb::new(
					raptor_math::Vec3f::from_array(min),
					raptor_math::Vec3f::from_array(max),
				),
			);
		}

		if let Some(skeleton) = node.skeleton.and_then(|index| skeletons.get(index))
			&& let Some(entry) = self.scene.node_mut(id)
		{
			entry.skeleton = Some(*skeleton);
		}

		for child in &node.children {
			if let Some(child) = self.instantiate_node(child, skeletons) {
				self.scene.attach_object(id, child);
			}
		}

		Some(id)
	}

	pub fn instantiate_model(&mut self, model: LoadedModel) -> Option<ObjectId> {
		let skeletons: Vec<SkeletonRef> = model
			.skeletons
			.into_iter()
			.map(|skeleton| self.store_skeleton(skeleton))
			.collect();

		self.instantiate_node(&model.root, &skeletons)
	}

	fn first_skeleton(&self, root: ObjectId) -> Option<SkeletonRef> {
		let own = self.scene.node(root).and_then(|node| node.skeleton);

		own.or_else(|| {
			self.scene
				.core(root)?
				.children()
				.iter()
				.find_map(|child| self.first_skeleton(*child))
		})
	}

	pub fn attach_loaded(&mut self, object: ObjectId) {
		self.scene.attach_loaded(object);
		self.services.object_attached(object);

		let children: Vec<ObjectId> = self
			.scene
			.core(object)
			.map(|core| core.children().to_vec())
			.unwrap_or_default();

		let shadow_caster = self
			.scene
			.core(object)
			.is_some_and(|core| core.has_flag(raptor_entity::object_core::FLAG_SHADOW_CASTER));

		for child in children {
			if shadow_caster {
				self.scene.set_shadow_caster(child, true);
			}

			self.scene.mark_added_to_world(child);
			self.scene.add_to_grid(child);
		}
	}

	pub fn detach(&mut self, object: ObjectId) {
		self.scene.detach(object);
		self.services.object_detached(object);
	}

	pub fn pump_models(&mut self) {
		for (ticket, result) in self.services.poll_models() {
			let Some(pending) = self.pending.remove(&ticket) else {
				continue;
			};

			let model = match result {
				Ok(model) => model,
				Err(error) => {
					raptor_core::log_error!("Could not load a model: {error}");
					continue;
				}
			};

			let Some(object) = self.instantiate_model(model) else {
				raptor_core::log_error!("No room for the objects of a model");
				continue;
			};

			match pending {
				Pending::ViewModel => self.finish_view_model(object),
				Pending::RagdollTemplate => {
					self.scene.set_shadow_caster(object, true);
					self.ragdoll_template = Some(object);
				}
				Pending::SceneObject(def) => {
					self.apply_object_def(object, &def);
					self.attach_loaded(object);
				}
			}
		}
	}

	fn finish_view_model(&mut self, object: ObjectId) {
		self.scene.set_shadow_caster(object, false);
		self.scene.set_cullable(object, false);
		self.scene.set_object_layer(object, LAYER_PLAYER);
		self.scene.set_probe_visible(object, false);

		self.view_model = Some(object);
		self.view_model_skeleton = self.first_skeleton(object);

		let idle = self.player.idle_animation.clone();

		if let Some(skeleton) = self
			.view_model_skeleton
			.and_then(|skeleton| self.skeleton_mut(skeleton))
			&& let Some(animation) = ViewModelRig::find_animation(skeleton, &idle)
		{
			ViewModelRig::set_rest_animation(skeleton, animation);
		}

		self.player.start_sway();
		self.attach_loaded(object);
	}

	pub fn set_fly_mode(&mut self, fly: bool) {
		self.player.set_fly_mode(fly);

		if let Some(character) = &mut self.player.character {
			character.set_collision_enabled(&mut self.scene.physics, !fly);
		}
	}

	pub fn set_sprinting(&mut self, sprinting: bool) {
		self.player.state.sprinting = u8::from(sprinting);
	}

	pub fn update_player(&mut self, delta_time: f64) {
		let head_bob = raptor_core::cvar::bool("b_headbob_enabled", false);

		self.player
			.update(&mut self.scene.physics, delta_time, head_bob);

		{
			let skeleton = self
				.view_model_skeleton
				.and_then(|skeleton| self.skeletons.get_mut(skeleton.0 as usize))
				.and_then(Option::as_mut);

			let mut env = Env {
				player: &mut self.player,
				scene: &mut self.scene,
				services: &mut *self.services,
				skeleton,
			};

			self.weapons.update(&mut env, delta_time as f32);
		}

		if let Some(view_model) = self.view_model {
			let (position, rotation) = self
				.player
				.view_model_pose(&self.scene.physics, delta_time as f32);

			self.scene.set_position(view_model, position);
			self.scene.set_rotation(view_model, rotation);
		}
	}

	pub fn weapons_set_input(&mut self, flags: u32) {
		self.weapons.set_input(InputFlags(flags as i32));
	}

	pub fn weapons_render_hud(&mut self) {
		let skeleton = None;

		let mut env = Env {
			player: &mut self.player,
			scene: &mut self.scene,
			services: &mut *self.services,
			skeleton,
		};

		self.weapons.render_hud(&mut env);
	}

	pub fn weapon_event(&mut self, event: Event) {
		let skeleton = self
			.view_model_skeleton
			.and_then(|skeleton| self.skeletons.get_mut(skeleton.0 as usize))
			.and_then(Option::as_mut);

		let mut env = Env {
			player: &mut self.player,
			scene: &mut self.scene,
			services: &mut *self.services,
			skeleton,
		};

		self.weapons.handle_event(&mut env, event);
	}

	pub fn physics_update(&mut self) {
		self.scene.physics.update();
	}

	pub fn update_objects(&mut self) {
		for id in self.scene.used_ids() {
			self.scene.update(id);
		}
	}

	pub fn camera_forward(&self) -> [f32; 3] {
		let direction = self.player.camera.direction;

		[direction[0], direction[1], direction[2]]
	}

	pub fn camera_right(&self) -> [f32; 3] {
		camera_basis([0.0; 3], self.camera_forward()).right
	}

	pub fn nearby_objects(&mut self) -> Vec<ObjectId> {
		self.scene.grid.nearby_objects().to_vec()
	}

	pub fn player_tile(&self) -> ([u32; 2], u32) {
		let tile = self.scene.grid.world_to_tile(self.player.position());

		(self.scene.grid.tile_to_xy(tile), tile)
	}

	fn collect_tree(&self, root: ObjectId, out: &mut Vec<ObjectId>) {
		out.push(root);

		if let Some(core) = self.scene.core(root) {
			for child in core.children() {
				self.collect_tree(*child, out);
			}
		}
	}

	fn destroy_tree(&mut self, root: ObjectId) {
		let mut parts = Vec::new();

		self.collect_tree(root, &mut parts);

		for part in &parts {
			self.detach(*part);
		}

		self.scene.destroy_object(root);
	}

	pub fn ragdoll_spawn(
		&mut self,
		position: [f32; 3],
		forward: [f32; 3],
		index: u32,
	) -> Option<(u32, u64)> {
		let template = self.ragdoll_template?;

		let name = format!("ragdoll_dummy_{index}");

		let skeletons = &mut self.skeletons;

		let root = self
			.scene
			.clone_with_own_skeleton(template, &name, &mut |source| {
				let copy = skeletons
					.get(source.0 as usize)
					.and_then(Option::as_ref)
					.map(Skeleton::create_instance);

				match copy {
					Some(copy) => store(skeletons, copy),
					None => source,
				}
			})?;

		let mut parts = Vec::new();

		self.collect_tree(root, &mut parts);

		let skinned = parts.iter().copied().find(|part| {
			self.scene
				.node(*part)
				.is_some_and(|node| node.skeleton.is_some())
		});

		let Some(skinned) = skinned else {
			raptor_core::log_warn!("The ragdoll model has no skeleton");
			self.destroy_tree(root);
			return None;
		};

		self.scene.set_position(root, position);

		for part in &parts {
			self.scene.set_cullable(*part, false);
		}

		self.scene.set_shadow_caster(root, true);
		self.scene.set_probe_visible(root, false);

		let skeleton_ref = self.scene.node(skinned)?.skeleton?;
		let object_world = self.scene.world_matrix(skinned)?;

		self.next_serial += 1;

		let serial = self.next_serial;

		let created = {
			let skeleton = self
				.skeletons
				.get_mut(skeleton_ref.0 as usize)
				.and_then(Option::as_mut)?;

			Ragdoll::create(
				&mut self.scene.physics,
				skeleton,
				CreateOptions {
					object_world: anim_matrix(&object_world),
					object_world_inverse: anim_matrix(&object_world.inverse()),
					serial,
					user_data: ragdoll_user_data(serial),
					log: &mut LogSink,
				},
			)
		};

		let Some(ragdoll) = created else {
			self.destroy_tree(root);
			return None;
		};

		self.attach_loaded(root);

		if let Some(skeleton) = self
			.skeletons
			.get_mut(skeleton_ref.0 as usize)
			.and_then(Option::as_mut)
		{
			ragdoll.activate(
				&mut self.scene.physics,
				skeleton,
				[forward[0] * 2.0, forward[1] * 2.0, forward[2] * 2.0],
			);
		}

		let handle = self.next_dummy;

		self.next_dummy += 1;

		self.dummies.insert(
			handle,
			Dummy {
				root,
				skinned,
				ragdoll,
			},
		);

		Some((handle, u64::from(serial)))
	}

	pub fn ragdoll_destroy(&mut self, handle: u32) {
		let Some(dummy) = self.dummies.remove(&handle) else {
			return;
		};

		let skeleton = self
			.scene
			.node(dummy.skinned)
			.and_then(|node| node.skeleton)
			.and_then(|skeleton| self.skeletons.get_mut(skeleton.0 as usize))
			.and_then(Option::as_mut);

		dummy.ragdoll.destroy(&mut self.scene.physics, skeleton);

		self.destroy_tree(dummy.root);
	}

	pub fn ragdoll_sync(&mut self, handle: u32) -> Vec<Mat4f> {
		let Some(dummy) = self.dummies.get_mut(&handle) else {
			return Vec::new();
		};

		if let Some(skeleton) = self
			.scene
			.node(dummy.skinned)
			.and_then(|node| node.skeleton)
			.and_then(|skeleton| self.skeletons.get_mut(skeleton.0 as usize))
			.and_then(Option::as_mut)
		{
			dummy
				.ragdoll
				.write_to_skeleton(&self.scene.physics, skeleton);
		}

		dummy
			.ragdoll
			.debug_boxes(&self.scene.physics)
			.iter()
			.map(|matrix| Mat4f::from_rows(&flatten(matrix)))
			.collect()
	}

	pub fn drain_ragdoll_impacts(&mut self, min_speed: f32) -> Vec<RagdollImpactInfo> {
		let backend_queue = self.scene.physics.backend().ragdoll_impacts();

		let Some(queue) = backend_queue else {
			return Vec::new();
		};

		queue.set_min_speed(min_speed);

		queue
			.drain()
			.into_iter()
			.map(|impact| RagdollImpactInfo {
				serial: impact.ragdoll_serial,
				point: impact.point,
				normal: impact.normal,
				speed: impact.speed,
			})
			.collect()
	}
}

fn store(skeletons: &mut Vec<Option<Skeleton>>, skeleton: Skeleton) -> SkeletonRef {
	let slot = skeletons
		.iter()
		.position(Option::is_none)
		.unwrap_or_else(|| {
			skeletons.push(None);
			skeletons.len() - 1
		});

	skeletons[slot] = Some(skeleton);

	SkeletonRef(slot as u32)
}

fn anim_matrix(matrix: &Mat4f) -> Mat4 {
	let rows = matrix.to_rows();

	Mat4([
		[rows[0], rows[1], rows[2], rows[3]],
		[rows[4], rows[5], rows[6], rows[7]],
		[rows[8], rows[9], rows[10], rows[11]],
		[rows[12], rows[13], rows[14], rows[15]],
	])
}

fn flatten(matrix: &Mat4) -> [f32; 16] {
	let mut out = [0.0; 16];

	for (row, values) in matrix.0.iter().enumerate() {
		out[row * 4..row * 4 + 4].copy_from_slice(values);
	}

	out
}

#[cfg(test)]
mod tests {
	use raptor_level::MaterialRegistry;
	use raptor_level::blockout::{CameraOut, Level, LightOut, SunOut};
	use raptor_mesh::Mesh;

	use raptor_editor::host::{BlockoutHost, ObjectHost, PhysicsHost};
	use raptor_math::Vec3f;

	use super::*;
	use crate::blockout::BlockoutEnv;
	use crate::scene::MeshRef;

	#[derive(Default)]
	struct Mock {
		next_ticket: u64,
		ready: Vec<(ModelTicket, Result<LoadedModel, String>)>,
		meshes: u32,
		holes: Vec<[f32; 3]>,
		blood: Vec<f32>,
		texts: Vec<String>,
		attached: Vec<ObjectId>,
	}

	impl BlockoutEnv for Mock {
		fn load_materials(&mut self) -> raptor_level::MaterialRegistry {
			let mut registry = MaterialRegistry::new();

			registry.push("default", 10);
			registry.push("editable", 11);

			registry
		}

		fn create_selection_material(&mut self) -> u32 {
			99
		}

		fn upload_mesh(&mut self, _mesh: &Mesh) -> MeshRef {
			self.meshes += 1;

			MeshRef {
				id: self.meshes,
				skinned: false,
			}
		}

		fn object_attached(&mut self, object: ObjectId) {
			self.attached.push(object);
		}

		fn object_detached(&mut self, _object: ObjectId) {}

		fn apply_environment(&mut self, _level: &Level, _grid: &mut raptor_world::WorldGrid) {}

		fn capture_environment(&mut self) -> (Option<SunOut>, Vec<LightOut>, CameraOut) {
			(
				None,
				Vec::new(),
				CameraOut {
					aperture: 16.0,
					shutter: 0.01,
					iso: 100.0,
					exposure_ev: 0.0,
				},
			)
		}

		fn forget_editor_objects(&mut self) {}

		fn stored_material(&self, _object: ObjectId) -> Option<u32> {
			None
		}
	}

	impl Services for Mock {
		fn request_model(&mut self, name: &str, _path: &str) -> ModelTicket {
			self.next_ticket += 1;

			let node = LoadedNode {
				name: name.to_owned(),
				mesh: Some(MeshRef {
					id: 1,
					skinned: false,
				}),
				bounds: Some(([-0.5; 3], [0.5; 3])),
				material: 5,
				skeleton: None,
				children: vec![LoadedNode {
					name: format!("{name}_part"),
					mesh: None,
					bounds: None,
					material: 0,
					skeleton: None,
					children: Vec::new(),
				}],
			};

			self.ready.push((
				self.next_ticket,
				Ok(LoadedModel {
					root: node,
					skeletons: Vec::new(),
				}),
			));

			self.next_ticket
		}

		fn poll_models(&mut self) -> Vec<(ModelTicket, Result<LoadedModel, String>)> {
			std::mem::take(&mut self.ready)
		}

		fn load_weapon_script(&mut self, _def: &WeaponDef) -> Box<dyn WeaponScript> {
			unreachable!()
		}

		fn add_bullet_hole(&mut self, point: [f32; 3], _normal: [f32; 3]) {
			self.holes.push(point);
		}

		fn add_blood_splat(&mut self, _point: [f32; 3], _normal: [f32; 3], size: f32) {
			self.blood.push(size);
		}

		fn window_size(&self) -> [u32; 2] {
			[800, 600]
		}

		fn glyph_size(&self) -> [f32; 2] {
			[6.0, 12.0]
		}

		fn draw_text(&mut self, text: &str, _position: [f32; 2], _scale: f32, _color: u32) {
			self.texts.push(text.to_owned());
		}

		fn random_unit(&mut self) -> f32 {
			0.5
		}
	}

	fn world() -> World<JoltBackend> {
		let mut world = World::new(
			JoltBackend::new().unwrap(),
			Box::new(Mock::default()),
			std::env::temp_dir().join("raptor-world-test-nothing"),
		);

		world.init_grid([8, 8]);

		world
	}

	fn object_def(name: &str) -> ObjectDef {
		ObjectDef {
			name: name.to_owned(),
			mesh: Some("/a.glb".to_owned()),
			shadows: Some(true),
			position: Some([1.0, 2.0, 3.0]),
			rotation: None,
			scale: Some(2.0),
			layer: None,
			unlit: true,
			no_cull: true,
			collider: None,
		}
	}

	#[test]
	fn a_scene_object_loads_with_its_properties_and_joins_the_grid() {
		let mut world = world();

		world.request_scene_object("scene/Models/a.glb", &object_def("crate"));
		world.pump_models();

		let id = world.scene.find_by_name("crate").unwrap();

		assert_eq!(world.scene.position(id), Some([1.0, 2.0, 3.0]));
		assert_eq!(world.scene.scale(id), Some(2.0));
		assert!(world.scene.in_grid(id));
		assert!(world.scene.node(id).unwrap().added_to_world);
		assert_eq!(world.scene.core(id).unwrap().children().len(), 1);

		let child = world.scene.core(id).unwrap().children()[0];

		assert!(world.scene.in_grid(child));
		assert!(
			world
				.scene
				.core(id)
				.unwrap()
				.has_flag(raptor_entity::object_core::FLAG_SHADOW_CASTER)
		);
		assert!(!world.scene.is_cullable(id));
	}

	#[test]
	fn a_second_load_updates_the_object_it_already_made() {
		let mut world = world();

		world.request_scene_object("scene/Models/a.glb", &object_def("crate"));
		world.pump_models();

		let mut moved = object_def("crate");

		moved.position = Some([9.0, 0.0, 0.0]);

		assert!(world.update_object_def(&moved));
		assert!(!world.update_object_def(&object_def("nothing")));

		let id = world.scene.find_by_name("crate").unwrap();

		assert_eq!(world.scene.position(id), Some([9.0, 0.0, 0.0]));
	}

	#[test]
	fn a_box_collider_from_a_scene_file_can_be_found_by_objects() {
		let mut world = world();

		world.add_collider(&ColliderDef {
			name: "floor".to_owned(),
			position: [0.0, -1.0, 0.0],
			rotation: [0.0, 0.0, 0.0, 1.0],
			dynamic: false,
			box_size: Some([20.0, 2.0, 20.0]),
		});

		let mut def = object_def("floor_mesh");

		def.collider = Some("floor".to_owned());

		world.request_scene_object("scene/Models/a.glb", &def);
		world.pump_models();

		let id = world.scene.find_by_name("floor_mesh").unwrap();

		assert!(world.scene.collider_of(id).is_some());
	}

	#[test]
	fn the_player_is_made_with_a_view_model_on_the_player_layer() {
		let mut world = world();

		world.begin_game(1.0);
		world.pump_models();

		let view_model = world.view_model.unwrap();

		assert_eq!(world.scene.core(view_model).unwrap().layer, LAYER_PLAYER);
		assert!(!world.scene.core(view_model).unwrap().is_probe_visible());

		for _ in 0..30 {
			world.physics_update();
			world.update_player(1.0 / 60.0);
		}

		let position = world.scene.position(view_model).unwrap();
		let camera = world.player.camera.position;

		let distance = ((position[0] - camera[0]).powi(2)
			+ (position[1] - camera[1]).powi(2)
			+ (position[2] - camera[2]).powi(2))
		.sqrt();

		assert!(distance < 1.0, "{distance}");
	}

	#[test]
	fn the_editor_can_make_edit_and_find_blocks_through_the_host_traits() {
		let mut world = world();

		world.create_blockout(None);

		let block = BlockoutHost::new_block(&mut world, Vec3f::new(0.0, 0.0, 6.0)).unwrap();

		assert!(ObjectHost::object_exists(&world, block));
		assert!(BlockoutHost::brush_planes(&world, block).is_some());

		let hits = PhysicsHost::raycast_objects(
			&world,
			Vec3f::new(0.0, 0.0, 0.0),
			Vec3f::new(0.0, 0.0, 20.0),
		);

		assert_eq!(hits, vec![block]);

		ObjectHost::set_object_tag(
			&mut world,
			block,
			raptor_entity::object_core::TAG_PROBE_VOLUME,
			true,
		);

		let nearest =
			ObjectHost::raycast_probe_volumes(&world, Vec3f::ZERO, Vec3f::new(0.0, 0.0, 1.0), 50.0);

		assert_eq!(nearest.map(|(id, _)| id), Some(block));

		BlockoutHost::destroy_block(&mut world, block);

		assert!(!ObjectHost::object_exists(&world, block));
	}

	#[test]
	fn the_ragdoll_cannot_be_spawned_before_its_template_loads() {
		let mut world = world();

		assert!(!world.ragdoll_template_ready());
		assert!(world.ragdoll_spawn([0.0; 3], [0.0, 0.0, 1.0], 0).is_none());

		world.request_ragdoll_template();
		world.pump_models();

		assert!(world.ragdoll_template_ready());
		assert!(world.ragdoll_spawn([0.0; 3], [0.0, 0.0, 1.0], 0).is_none());
	}

	#[test]
	fn the_player_tile_and_nearby_objects_follow_the_grid() {
		let mut world = world();

		let (xy, tile) = world.player_tile();

		assert_eq!(world.scene.grid.tile_from_xy(xy), tile);
		assert!(world.nearby_objects().is_empty());
	}
}
