use raptor_gpu::VertexType;
use raptor_mesh::Mesh;
use raptor_render::mesh_pack::Attributes;
use raptor_render::mesh_record::MeshRecord;

pub use raptor_mesh::{CubeOptions, FaceOptions, cube, icosphere, line, quad, wireframe_box};

fn flatten<const N: usize>(values: &[[f32; N]]) -> Vec<f32> {
	values.iter().flatten().copied().collect()
}

#[derive(Debug, PartialEq, Eq)]
pub enum MeshGenError {
	UnsupportedVertexType,
	NoVertices,
}

/// What a generated mesh becomes on the CPU, for the renderer to put on the GPU
pub trait IntoMeshRecord {
	fn into_record(&self, vertex_type: VertexType) -> Result<MeshRecord, MeshGenError>;

	fn into_slim_record(&self) -> Result<MeshRecord, MeshGenError> {
		self.into_record(VertexType::Slim)
	}

	fn into_default_record(&self) -> Result<MeshRecord, MeshGenError> {
		self.into_record(VertexType::Default)
	}
}

impl IntoMeshRecord for Mesh {
	fn into_record(&self, vertex_type: VertexType) -> Result<MeshRecord, MeshGenError> {
		let mut record = MeshRecord::default();

		match vertex_type {
			VertexType::Slim => {
				let bytes: Vec<u8> = self
					.positions
					.iter()
					.flatten()
					.flat_map(|value| value.to_ne_bytes())
					.collect();

				record.set_vertices(VertexType::Slim, &bytes);
			}
			VertexType::Default => {
				record.keep_in_memory = true;

				let positions = flatten(&self.positions);
				let normals = flatten(&self.normals);
				let uvs = flatten(&self.texcoords);
				let tangents = flatten(&self.tangents);

				let packed = record.pack(&Attributes {
					positions: &positions,
					normals: &normals,
					uvs: &uvs,
					tangents: &tangents,
					tangent_stride: 3,
					handedness: 1.0,
					..Attributes::default()
				});

				if !packed {
					return Err(MeshGenError::NoVertices);
				}
			}
			VertexType::Skinned => return Err(MeshGenError::UnsupportedVertexType),
		}

		record.set_indices(&self.indices);

		Ok(record)
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn a_slim_mesh_keeps_only_the_positions() {
		let record = quad(1.0, 1.0).into_slim_record().unwrap();

		assert_eq!(record.vertex_type(), VertexType::Slim);
		assert_eq!(record.vertex_count(), 4);
		assert_eq!(record.indices().len(), 6);
	}

	#[test]
	fn a_default_mesh_has_normals_uvs_and_tangents() {
		let record = cube(&CubeOptions::default()).into_default_record().unwrap();

		assert_eq!(record.vertex_type(), VertexType::Default);
		assert!(record.has_normals() && record.has_uvs());
		assert!(record.keep_in_memory);
	}

	#[test]
	fn skinned_meshes_cannot_be_generated() {
		assert_eq!(
			line().into_record(VertexType::Skinned).err(),
			Some(MeshGenError::UnsupportedVertexType)
		);
	}
}
