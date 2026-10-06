use std::sync::{Arc, Mutex, MutexGuard};

use raptor_gfx::{Gfx, GpuCore};
use raptor_gpu::VertexType;
use raptor_mesh::Mesh;
use raptor_render::primitive_mesh::PrimitiveMesh;

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T>
{
	mutex
		.lock()
		.unwrap_or_else(|poisoned| poisoned.into_inner())
}

/// The meshes the objects draw, by the id an object's mesh reference holds.
pub struct MeshStore
{
	core: Arc<GpuCore>,
	slots: Mutex<Vec<Option<Arc<Mutex<PrimitiveMesh>>>>>,
}

impl MeshStore
{
	pub fn new(core: Arc<GpuCore>) -> Self
	{
		Self {
			core,
			slots: Mutex::new(vec![None]),
		}
	}

	pub fn core(&self) -> &Arc<GpuCore>
	{
		&self.core
	}

	pub fn add(&self, mesh: PrimitiveMesh) -> u32
	{
		let mut slots = lock(&self.slots);

		let mesh = Arc::new(Mutex::new(mesh));

		match slots.iter().skip(1).position(Option::is_none) {
			Some(index) => {
				slots[index + 1] = Some(mesh);

				(index + 1) as u32
			}
			None => {
				slots.push(Some(mesh));

				(slots.len() - 1) as u32
			}
		}
	}

	pub fn from_mesh(&self, gfx: &Gfx, mesh: &Mesh) -> Option<u32>
	{
		let mut primitive = PrimitiveMesh::from_mesh(&self.core, mesh, VertexType::Default);

		if let Err(error) = primitive.upload_now(gfx) {
			raptor_core::log_error!(Render; "Could not upload a generated mesh: {error:?}");
			return None;
		}

		Some(self.add(primitive))
	}

	pub fn get(&self, id: u32) -> Option<Arc<Mutex<PrimitiveMesh>>>
	{
		lock(&self.slots).get(id as usize)?.clone()
	}

	pub fn is_ready(&self, id: u32) -> bool
	{
		self.get(id).is_some_and(|mesh| lock(&mesh).is_ready())
	}

	pub fn remove(&self, id: u32)
	{
		if let Some(slot) = lock(&self.slots).get_mut(id as usize) {
			*slot = None;
		}
	}

	pub fn clear(&self)
	{
		lock(&self.slots).clear();
	}

	pub fn count(&self) -> usize
	{
		lock(&self.slots).iter().flatten().count()
	}
}
