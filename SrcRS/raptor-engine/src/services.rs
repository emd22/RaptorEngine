use std::cell::RefCell;
use std::collections::HashMap;
use std::rc::Rc;
use std::sync::{Arc, Mutex, MutexGuard, PoisonError};

use raptor_asset::manager::AssetManager;
use raptor_asset::materials::MaterialStore;
use raptor_asset::model::{Model, ModelNode, ModelOptions};
use raptor_asset::ticket::Ticket;
use raptor_gfx::{Gfx, Uploader};
use raptor_level::MaterialRegistry;
use raptor_level::WeaponDef;
use raptor_level::blockout::{CameraOut, Level, LightOut, SunOut};
use raptor_mesh::Mesh;
use raptor_render::primitive_mesh::PrimitiveMesh;
use raptor_scene::blockout::BlockoutEnv;
use raptor_scene::scene::{MeshRef, ObjectId};
use raptor_scene::services::{LoadedModel, LoadedNode, ModelTicket, Services};
use raptor_script::ScriptManager;
use raptor_weapon::WeaponScript;
use raptor_weapon::strata::StrataWeapon;
use raptor_world::WorldGrid;

use crate::meshes::MeshStore;

pub const GLYPH_SIZE: [f32; 2] = [6.0, 12.0];

pub const MATERIAL_LIST_PATH: &str = "RaptorData/Data/materials/list.prx";
pub const MATERIAL_TEXTURE_ROOT: &str = "RaptorData/Data/materials";
pub const SELECTION_TEXTURE_PATH: &str = "RaptorData/Data/Demo/Textures/aqua_check.png";

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T>
{
	mutex.lock().unwrap_or_else(PoisonError::into_inner)
}

struct Handoff(LoadedModel);

// SAFETY: a loaded model holds skeletons whose raw pointers point into their own heap vectors,
// which move with them, and it is only used by one thread at a time.
unsafe impl Send for Handoff {}

struct Request
{
	ticket: Arc<Ticket>,
	slot: Arc<Mutex<Option<Handoff>>>,
}

pub struct DecalRequest
{
	pub point: [f32; 3],
	pub normal: [f32; 3],
	pub size: Option<f32>,
}

pub struct TextRequest
{
	pub text: String,
	pub position: [f32; 2],
	pub scale: f32,
	pub color: u32,
}

#[derive(Default)]
pub struct Environment
{
	pub sun: Option<SunOut>,
	pub lights: Vec<LightOut>,
	pub camera: Option<CameraOut>,
}

pub struct State
{
	pub gfx: Arc<Gfx>,
	pub assets: Arc<AssetManager>,
	pub materials: Arc<MaterialStore>,
	pub meshes: Arc<MeshStore>,
	pub scripts: Arc<Mutex<ScriptManager>>,
	pub registry: MaterialRegistry,
	pub selection_material: u32,
	pub requests: HashMap<ModelTicket, Request>,
	pub next_ticket: ModelTicket,
	pub decals: Vec<DecalRequest>,
	pub texts: Vec<TextRequest>,
	pub levels: Vec<Level>,
	pub environment: Environment,
	pub detached: Vec<ObjectId>,
	pub base: std::path::PathBuf,
}

/// What the world asks of the engine, shared with the engine so that what it queues can be picked up.
#[derive(Clone)]
pub struct EngineServices
{
	pub state: Rc<RefCell<State>>,
}

impl EngineServices
{
	pub fn new(state: State) -> Self
	{
		Self {
			state: Rc::new(RefCell::new(state)),
		}
	}
}

fn convert_node(
	node: ModelNode,
	assets: &AssetManager,
	materials: &MaterialStore,
	meshes: &MeshStore,
	uploader: &Uploader,
) -> LoadedNode
{
	let mesh = node.mesh.and_then(|record| {
		let skinned = record.is_skinned();

		let mut primitive = PrimitiveMesh::new(meshes.core());

		primitive.record = record;

		let uploaded = primitive
			.upload_vertices(uploader)
			.and_then(|()| primitive.upload_indices(uploader));

		if let Err(error) = uploaded {
			raptor_core::log_error!(Asset; "Could not upload a mesh of '{}': {error:?}", node.name);
			return None;
		}

		primitive.record.set_ready(true);

		Some(MeshRef {
			id: meshes.add(primitive),
			skinned,
		})
	});

	let material = node
		.material
		.and_then(|desc| materials.create_from_desc(desc, assets))
		.map_or(0, |(id, _)| id.0);

	LoadedNode {
		name: node.name,
		mesh,
		bounds: node.bounds,
		material,
		skeleton: node.skeleton,
		children: node
			.children
			.into_iter()
			.map(|child| convert_node(child, assets, materials, meshes, uploader))
			.collect(),
	}
}

impl BlockoutEnv for EngineServices
{
	fn load_materials(&mut self) -> MaterialRegistry
	{
		let state = self.state.borrow();

		let mut library = raptor_asset::material_library::MaterialLibrary::new();

		library.load(
			&state.materials,
			&state.assets,
			MATERIAL_LIST_PATH,
			MATERIAL_TEXTURE_ROOT,
		);

		let mut registry = MaterialRegistry::new();

		for index in 0..library.count() {
			registry.push(library.name(index), library.material(index as i32).0);
		}

		registry
	}

	fn create_selection_material(&mut self) -> u32
	{
		use raptor_asset::manager::ImageRequest;
		use raptor_asset::materials::ResourceKind;
		use raptor_gpu::ImageFormat;

		let mut state = self.state.borrow_mut();

		let Some((id, material)) = state.materials.create("ProtoSelect", false) else {
			return 0;
		};

		let diffuse = state.assets.load_image(
			raptor_asset::fs::resolve_path(SELECTION_TEXTURE_PATH),
			ImageRequest::flat(ImageFormat::Rgba8UNorm),
		);

		material.with_record(|record| record.set_alpha(0.7));
		material.attach(ResourceKind::Diffuse, diffuse);
		material.finalize();

		state.selection_material = id.0;

		id.0
	}

	fn upload_mesh(&mut self, mesh: &Mesh) -> MeshRef
	{
		let state = self.state.borrow();

		let id = state.meshes.from_mesh(&state.gfx, mesh).unwrap_or(0);

		MeshRef { id, skinned: false }
	}

	fn object_attached(&mut self, _object: ObjectId) {}

	fn object_detached(&mut self, object: ObjectId)
	{
		self.state.borrow_mut().detached.push(object);
	}

	fn apply_environment(&mut self, level: &Level, _grid: &mut WorldGrid)
	{
		self.state.borrow_mut().levels.push(Level {
			has_errors: level.has_errors,
			has_blocks: level.has_blocks,
			sun: level.sun.clone(),
			lights: level.lights.clone(),
			camera: level.camera.clone(),
			blocks: Vec::new(),
		});
	}

	fn capture_environment(&mut self) -> (Option<SunOut>, Vec<LightOut>, CameraOut)
	{
		let state = self.state.borrow();

		(
			state.environment.sun.clone(),
			state.environment.lights.clone(),
			state.environment.camera.unwrap_or(CameraOut {
				aperture: 16.0,
				shutter: 0.01,
				iso: 100.0,
				exposure_ev: 0.0,
			}),
		)
	}

	fn forget_editor_objects(&mut self) {}

	fn stored_material(&self, _object: ObjectId) -> Option<u32>
	{
		None
	}
}

impl Services for EngineServices
{
	fn request_model(&mut self, name: &str, path: &str) -> ModelTicket
	{
		let mut state = self.state.borrow_mut();

		state.next_ticket += 1;

		let id = state.next_ticket;

		let slot: Arc<Mutex<Option<Handoff>>> = Arc::new(Mutex::new(None));

		let assets = Arc::clone(&state.assets);
		let materials = Arc::clone(&state.materials);
		let meshes = Arc::clone(&state.meshes);

		let target = Arc::clone(&slot);

		let builder = {
			let assets = Arc::clone(&assets);

			Box::new(move |model: Model, uploader: &Uploader| {
				let root = convert_node(model.root, &assets, &materials, &meshes, uploader);

				*lock(&target) = Some(Handoff(LoadedModel {
					root,
					skeletons: model.skeletons,
				}));
			})
		};

		let ticket = assets.load_model(
			name,
			raptor_asset::fs::resolve_path(path),
			ModelOptions::default(),
			builder,
		);

		state.requests.insert(id, Request { ticket, slot });

		id
	}

	fn poll_models(&mut self) -> Vec<(ModelTicket, Result<LoadedModel, String>)>
	{
		let mut state = self.state.borrow_mut();

		let done: Vec<ModelTicket> = state
			.requests
			.iter()
			.filter(|(_, request)| request.ticket.is_finished())
			.map(|(id, _)| *id)
			.collect();

		done.into_iter()
			.filter_map(|id| {
				let request = state.requests.remove(&id)?;

				let result = match lock(&request.slot).take() {
					Some(handoff) if request.ticket.is_loaded() => Ok(handoff.0),
					_ => Err("the model could not be loaded".to_owned()),
				};

				Some((id, result))
			})
			.collect()
	}

	fn load_weapon_script(&mut self, def: &WeaponDef) -> Box<dyn WeaponScript>
	{
		let state = self.state.borrow();

		let path = state.base.join(&def.script);

		Box::new(StrataWeapon::load(
			Arc::clone(&state.scripts),
			&path.to_string_lossy(),
			&def.name,
		))
	}

	fn add_bullet_hole(&mut self, point: [f32; 3], normal: [f32; 3])
	{
		self.state.borrow_mut().decals.push(DecalRequest {
			point,
			normal,
			size: None,
		});
	}

	fn add_blood_splat(&mut self, point: [f32; 3], normal: [f32; 3], size: f32)
	{
		self.state.borrow_mut().decals.push(DecalRequest {
			point,
			normal,
			size: Some(size),
		});
	}

	fn window_size(&self) -> [u32; 2]
	{
		let (width, height) = self.state.borrow().gfx.window().size();

		[width, height]
	}

	fn glyph_size(&self) -> [f32; 2]
	{
		GLYPH_SIZE
	}

	fn draw_text(&mut self, text: &str, position: [f32; 2], scale: f32, color: u32)
	{
		self.state.borrow_mut().texts.push(TextRequest {
			text: text.to_owned(),
			position,
			scale,
			color,
		});
	}

	fn random_unit(&mut self) -> f32
	{
		raptor_world::random::unit()
	}
}
