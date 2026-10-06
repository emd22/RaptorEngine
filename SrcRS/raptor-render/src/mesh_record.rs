use std::sync::atomic::{AtomicBool, Ordering};

use raptor_gpu::VertexType;

use crate::mesh_pack::{Attributes, Packed, pack_vertices, recalculate_normals};

const NORMALS: u32 = 1;
const UVS: u32 = 2;
const TANGENTS: u32 = 4;

#[derive(Debug, PartialEq, Eq)]
pub enum NormalsStatus
{
	Present,
	Recalculated,
	MissingIndices,
	MissingVertices,
	IndicesDoNotFit,
}

/// A mesh's vertices and indices as the CPU has them, and what state it is in. The vertex bytes
/// are laid out as the vertex type says, with the position first.
#[derive(Debug)]
pub struct MeshRecord
{
	vertex_type: VertexType,
	flags: u32,
	vertices: Vec<u8>,
	stride: usize,
	indices: Vec<u32>,
	pub ready: AtomicBool,
	pub is_reference: bool,
	pub keep_in_memory: bool,
}

impl Default for MeshRecord
{
	fn default() -> Self
	{
		Self {
			vertex_type: VertexType::Default,
			flags: 0,
			vertices: Vec::new(),
			stride: VertexType::Default.stride() as usize,
			indices: Vec::new(),
			ready: AtomicBool::new(false),
			is_reference: false,
			keep_in_memory: false,
		}
	}
}

impl MeshRecord
{
	pub fn vertex_type(&self) -> VertexType
	{
		self.vertex_type
	}

	pub fn is_skinned(&self) -> bool
	{
		self.vertex_type == VertexType::Skinned
	}

	pub fn supports_normals(&self) -> bool
	{
		self.vertex_type != VertexType::Slim
	}

	pub fn has_normals(&self) -> bool
	{
		self.flags & NORMALS != 0
	}

	pub fn has_uvs(&self) -> bool
	{
		self.flags & UVS != 0
	}

	pub fn has_tangents(&self) -> bool
	{
		self.flags & TANGENTS != 0
	}

	pub fn is_ready(&self) -> bool
	{
		self.ready.load(Ordering::Acquire)
	}

	pub fn set_ready(&self, ready: bool)
	{
		self.ready.store(ready, Ordering::Release);
	}

	pub fn vertex_count(&self) -> usize
	{
		self.vertices.len().checked_div(self.stride).unwrap_or(0)
	}

	pub fn stride(&self) -> usize
	{
		self.stride
	}

	pub fn vertex_bytes(&self) -> &[u8]
	{
		&self.vertices
	}

	pub fn indices(&self) -> &[u32]
	{
		&self.indices
	}

	pub fn set_indices(&mut self, indices: &[u32])
	{
		self.indices = indices.to_vec();
	}

	/// Takes the vertices as they were packed.
	pub fn set_packed(&mut self, packed: Packed)
	{
		self.vertex_type = packed.vertex_type;
		self.stride = packed.vertex_type.stride() as usize;
		self.vertices = packed.bytes;
		self.flags = [
			(packed.has_normals, NORMALS),
			(packed.has_uvs, UVS),
			(packed.has_tangents, TANGENTS),
		]
		.into_iter()
		.filter(|(present, _)| *present)
		.fold(0, |flags, (_, bit)| flags | bit);
	}

	pub fn pack(&mut self, attributes: &Attributes) -> bool
	{
		pack_vertices(attributes)
			.map(|packed| self.set_packed(packed))
			.is_some()
	}

	/// Takes vertices that are already laid out for `vertex_type`.
	pub fn set_vertices(&mut self, vertex_type: VertexType, bytes: &[u8])
	{
		self.vertex_type = vertex_type;
		self.stride = vertex_type.stride() as usize;
		self.vertices = bytes.to_vec();
		self.flags = 0;
	}

	/// Works the normals out from the triangles if the vertex type has room for them and they were
	/// not given.
	pub fn ensure_normals(&mut self) -> NormalsStatus
	{
		if !self.supports_normals() || self.has_normals() {
			return NormalsStatus::Present;
		}

		if self.indices.is_empty() {
			return NormalsStatus::MissingIndices;
		}

		if self.vertices.is_empty() {
			return NormalsStatus::MissingVertices;
		}

		if !recalculate_normals(&mut self.vertices, self.stride, &self.indices) {
			return NormalsStatus::IndicesDoNotFit;
		}

		self.flags |= NORMALS;

		NormalsStatus::Recalculated
	}

	fn position(&self, index: usize) -> [f32; 3]
	{
		let at = index * self.stride;
		let float = |offset: usize| {
			let mut bytes = [0u8; 4];
			bytes.copy_from_slice(&self.vertices[at + offset..at + offset + 4]);

			f32::from_ne_bytes(bytes)
		};

		[float(0), float(4), float(8)]
	}

	/// Every vertex position, as consecutive x y z.
	pub fn positions(&self) -> Vec<f32>
	{
		(0..self.vertex_count())
			.flat_map(|index| self.position(index))
			.collect()
	}

	/// The box around the vertices, or `None` when there are none.
	pub fn bounds(&self) -> Option<([f32; 3], [f32; 3])>
	{
		if self.vertex_count() == 0 {
			return None;
		}

		let mut min = [f32::MAX; 3];
		let mut max = [-f32::MAX; 3];

		for index in 0..self.vertex_count() {
			let position = self.position(index);

			for axis in 0..3 {
				min[axis] = min[axis].min(position[axis]);
				max[axis] = max[axis].max(position[axis]);
			}
		}

		Some((min, max))
	}

	pub fn clear_local(&mut self)
	{
		self.vertices = Vec::new();
		self.indices = Vec::new();
	}

	pub fn clear_vertices(&mut self)
	{
		self.vertices = Vec::new();
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn quad() -> MeshRecord
	{
		let mut mesh = MeshRecord::default();

		let positions = [0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 1.0];
		let uvs = [0.0; 8];

		assert!(mesh.pack(&Attributes {
			positions: &positions,
			uvs: &uvs,
			tangent_stride: 3,
			handedness: 1.0,
			..Attributes::default()
		}));

		mesh
	}

	#[test]
	fn packing_sets_the_type_count_and_flags()
	{
		let mesh = quad();

		assert_eq!(mesh.vertex_type(), VertexType::Default);
		assert_eq!(mesh.vertex_count(), 4);
		assert!(mesh.has_uvs() && !mesh.has_normals());
		assert!(!mesh.is_skinned() && mesh.supports_normals());
	}

	#[test]
	fn bounds_and_positions_follow_the_vertices()
	{
		let mesh = quad();

		assert_eq!(mesh.bounds(), Some(([0.0, 0.0, 0.0], [1.0, 0.0, 1.0])));
		assert_eq!(mesh.positions().len(), 12);
		assert_eq!(MeshRecord::default().bounds(), None);
	}

	#[test]
	fn normals_are_worked_out_once_there_are_indices()
	{
		let mut mesh = quad();

		assert_eq!(mesh.ensure_normals(), NormalsStatus::MissingIndices);

		mesh.set_indices(&[0, 2, 1, 0, 3, 2]);

		assert_eq!(mesh.ensure_normals(), NormalsStatus::Recalculated);
		assert!(mesh.has_normals());
		assert_eq!(mesh.ensure_normals(), NormalsStatus::Present);
	}

	#[test]
	fn indices_past_the_vertices_are_refused()
	{
		let mut mesh = quad();

		mesh.set_indices(&[0, 1, 9]);

		assert_eq!(mesh.ensure_normals(), NormalsStatus::IndicesDoNotFit);
		assert!(!mesh.has_normals());
	}

	#[test]
	fn a_slim_mesh_has_no_use_for_normals()
	{
		let mut mesh = MeshRecord::default();

		mesh.set_vertices(VertexType::Slim, &[0u8; 24]);

		assert_eq!(mesh.vertex_count(), 2);
		assert_eq!(mesh.ensure_normals(), NormalsStatus::Present);
	}

	#[test]
	fn the_ready_flag_is_shared_across_threads()
	{
		let mesh = MeshRecord::default();

		assert!(!mesh.is_ready());

		mesh.set_ready(true);

		assert!(mesh.is_ready());
	}
}
