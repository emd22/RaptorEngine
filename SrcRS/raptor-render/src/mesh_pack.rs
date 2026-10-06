use raptor_gpu::VertexType;

const POSITION_BYTES: usize = 12;
const NORMAL_OFFSET: usize = 12;
const UV_OFFSET: usize = 24;
const TANGENT_OFFSET: usize = 32;
const BONE_IDS_OFFSET: usize = 48;
const BONE_WEIGHTS_OFFSET: usize = 64;

#[derive(Default)]
pub struct Attributes<'a>
{
	pub positions: &'a [f32],
	pub normals: &'a [f32],
	pub uvs: &'a [f32],
	pub tangents: &'a [f32],
	pub tangent_stride: usize,
	pub handedness: f32,
	pub bone_weights: &'a [f32],
	pub bone_ids: &'a [u32],
	pub negative_x: bool,
	pub mirror_basis: bool,
}

pub struct Packed
{
	pub vertex_type: VertexType,
	pub bytes: Vec<u8>,
	pub count: usize,
	pub has_normals: bool,
	pub has_uvs: bool,
	pub has_tangents: bool,
}

fn put(bytes: &mut [u8], offset: usize, values: &[f32])
{
	for (index, value) in values.iter().enumerate() {
		let at = offset + index * 4;
		bytes[at..at + 4].copy_from_slice(&value.to_ne_bytes());
	}
}

pub fn pack_vertices(attributes: &Attributes) -> Option<Packed>
{
	let count = attributes.positions.len() / 3;

	if count == 0 || !attributes.positions.len().is_multiple_of(3) {
		return None;
	}

	let has_normals = !attributes.normals.is_empty();
	let has_uvs = !attributes.uvs.is_empty();
	let has_tangents = !attributes.tangents.is_empty();
	let has_skin = !attributes.bone_weights.is_empty() && !attributes.bone_ids.is_empty();

	let vertex_type = if has_skin {
		VertexType::Skinned
	} else if has_normals || has_uvs || has_tangents {
		VertexType::Default
	} else {
		VertexType::Slim
	};

	let stride = vertex_type.stride() as usize;

	if has_normals && attributes.normals.len() < count * 3
		|| has_uvs && attributes.uvs.len() < count * 2
		|| has_tangents && attributes.tangents.len() < count * attributes.tangent_stride
		|| has_skin
			&& (attributes.bone_weights.len() < count * 4 || attributes.bone_ids.len() < count * 4)
	{
		return None;
	}

	let mut bytes = vec![0u8; stride * count];

	for index in 0..count {
		let vertex = &mut bytes[index * stride..(index + 1) * stride];

		let mut position = [0.0; 3];
		position.copy_from_slice(&attributes.positions[index * 3..index * 3 + 3]);

		if attributes.negative_x {
			position[0] = -position[0];
		}

		put(vertex, 0, &position);

		if vertex_type == VertexType::Slim {
			continue;
		}

		let mut normal = [0.0; 3];
		let mut uv = [0.0; 2];
		let mut tangent = [0.0, 0.0, 0.0, attributes.handedness];

		if has_normals {
			normal.copy_from_slice(&attributes.normals[index * 3..index * 3 + 3]);
		}

		if has_uvs {
			uv.copy_from_slice(&attributes.uvs[index * 2..index * 2 + 2]);
		}

		if has_tangents {
			let stride = attributes.tangent_stride;
			let source = &attributes.tangents[index * stride..index * stride + stride];

			tangent[..stride].copy_from_slice(source);
		}

		if attributes.mirror_basis {
			normal[0] = -normal[0];
			tangent[0] = -tangent[0];
			tangent[3] = -tangent[3];
		}

		put(vertex, NORMAL_OFFSET, &normal);
		put(vertex, UV_OFFSET, &uv);
		put(vertex, TANGENT_OFFSET, &tangent);

		if has_skin {
			for lane in 0..4 {
				let at = BONE_IDS_OFFSET + lane * 4;

				vertex[at..at + 4]
					.copy_from_slice(&attributes.bone_ids[index * 4 + lane].to_ne_bytes());
			}

			put(
				vertex,
				BONE_WEIGHTS_OFFSET,
				&attributes.bone_weights[index * 4..index * 4 + 4],
			);
		}
	}

	Some(Packed {
		vertex_type,
		bytes,
		count,
		has_normals,
		has_uvs,
		has_tangents,
	})
}

fn read_vec3(bytes: &[u8], offset: usize) -> [f32; 3]
{
	std::array::from_fn(|axis| {
		let at = offset + axis * 4;
		f32::from_ne_bytes([bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]])
	})
}

/// Gives each vertex the area weighted average of the normals of the triangles that use it.
/// Vertices are `stride` bytes with the position first and the normal at the offset of the default
/// vertex.
pub fn recalculate_normals(vertices: &mut [u8], stride: usize, indices: &[u32]) -> bool
{
	if stride < NORMAL_OFFSET + POSITION_BYTES || !vertices.len().is_multiple_of(stride) {
		return false;
	}

	let count = vertices.len() / stride;

	if indices.iter().any(|index| *index as usize >= count) {
		return false;
	}

	let mut normals = vec![[0.0f32; 3]; count];

	for triangle in indices.as_chunks::<3>().0.iter() {
		let position = |index: u32| read_vec3(vertices, index as usize * stride);

		let (a, b, c) = (
			position(triangle[0]),
			position(triangle[1]),
			position(triangle[2]),
		);

		let edge_a: [f32; 3] = std::array::from_fn(|axis| a[axis] - b[axis]);
		let edge_b: [f32; 3] = std::array::from_fn(|axis| c[axis] - b[axis]);

		let normal = [
			edge_a[1] * edge_b[2] - edge_a[2] * edge_b[1],
			edge_a[2] * edge_b[0] - edge_a[0] * edge_b[2],
			edge_a[0] * edge_b[1] - edge_a[1] * edge_b[0],
		];

		for index in triangle {
			for (sum, value) in normals[*index as usize].iter_mut().zip(normal) {
				*sum += value;
			}
		}
	}

	for (index, normal) in normals.iter().enumerate() {
		let length = (normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]).sqrt();

		let unit = if length > 0.0 {
			[normal[0] / length, normal[1] / length, normal[2] / length]
		} else {
			[0.0; 3]
		};

		put(&mut vertices[index * stride..], NORMAL_OFFSET, &unit);
	}

	true
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn read(bytes: &[u8], offset: usize, count: usize) -> Vec<f32>
	{
		(0..count)
			.map(|i| {
				let at = offset + i * 4;
				f32::from_ne_bytes(bytes[at..at + 4].try_into().unwrap())
			})
			.collect()
	}

	#[test]
	fn positions_alone_make_slim_vertices()
	{
		let packed = pack_vertices(&Attributes {
			positions: &[1.0, 2.0, 3.0, 4.0, 5.0, 6.0],
			negative_x: true,
			..Default::default()
		})
		.unwrap();

		assert_eq!(packed.vertex_type, VertexType::Slim);
		assert_eq!(packed.count, 2);
		assert_eq!(read(&packed.bytes, 12, 3), vec![-4.0, 5.0, 6.0]);
	}

	#[test]
	fn gltf_style_mirroring_flips_the_basis()
	{
		let packed = pack_vertices(&Attributes {
			positions: &[1.0, 0.0, 0.0],
			normals: &[1.0, 0.0, 0.0],
			uvs: &[0.5, 0.25],
			tangents: &[0.0, 1.0, 0.0, 1.0],
			tangent_stride: 4,
			handedness: 1.0,
			negative_x: true,
			mirror_basis: true,
			..Default::default()
		})
		.unwrap();

		assert_eq!(packed.vertex_type, VertexType::Default);
		assert_eq!(read(&packed.bytes, 0, 3), vec![-1.0, 0.0, 0.0]);
		assert_eq!(read(&packed.bytes, NORMAL_OFFSET, 3), vec![-1.0, 0.0, 0.0]);
		assert_eq!(read(&packed.bytes, UV_OFFSET, 2), vec![0.5, 0.25]);
		assert_eq!(
			read(&packed.bytes, TANGENT_OFFSET, 4),
			vec![-0.0, 1.0, 0.0, -1.0]
		);
	}

	#[test]
	fn missing_tangents_take_the_given_handedness()
	{
		let packed = pack_vertices(&Attributes {
			positions: &[0.0; 3],
			normals: &[0.0, 1.0, 0.0],
			tangent_stride: 3,
			handedness: -1.0,
			..Default::default()
		})
		.unwrap();

		assert_eq!(
			read(&packed.bytes, TANGENT_OFFSET, 4),
			vec![0.0, 0.0, 0.0, -1.0]
		);
		assert!(packed.has_normals && !packed.has_tangents);
	}

	#[test]
	fn skin_data_makes_skinned_vertices()
	{
		let packed = pack_vertices(&Attributes {
			positions: &[0.0; 3],
			bone_weights: &[0.5, 0.5, 0.0, 0.0],
			bone_ids: &[7, 8, 0, 0],
			tangent_stride: 4,
			handedness: 1.0,
			..Default::default()
		})
		.unwrap();

		assert_eq!(packed.vertex_type, VertexType::Skinned);
		assert_eq!(packed.bytes.len(), 80);
		assert_eq!(
			u32::from_ne_bytes(packed.bytes[48..52].try_into().unwrap()),
			7
		);
		assert_eq!(read(&packed.bytes, BONE_WEIGHTS_OFFSET, 2), vec![0.5, 0.5]);
	}

	#[test]
	fn short_attribute_arrays_are_rejected()
	{
		assert!(
			pack_vertices(&Attributes {
				positions: &[0.0; 6],
				normals: &[0.0; 3],
				..Default::default()
			})
			.is_none()
		);
	}

	#[test]
	fn a_flat_triangle_gets_the_normal_of_its_face_on_every_vertex()
	{
		let mut packed = pack_vertices(&Attributes {
			positions: &[0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0],
			normals: &[0.0; 9],
			tangent_stride: 3,
			handedness: 1.0,
			..Default::default()
		})
		.unwrap();

		assert!(recalculate_normals(&mut packed.bytes, 48, &[0, 1, 2]));

		for vertex in 0..3 {
			let normal = read(&packed.bytes, vertex * 48 + NORMAL_OFFSET, 3);
			assert!(normal[0].abs() < 1e-6 && normal[2].abs() < 1e-6);
			assert!((normal[1].abs() - 1.0).abs() < 1e-6);
		}
	}

	#[test]
	fn two_triangles_sharing_a_vertex_average_its_normal()
	{
		let mut packed = pack_vertices(&Attributes {
			positions: &[0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0],
			normals: &[0.0; 12],
			tangent_stride: 3,
			handedness: 1.0,
			..Default::default()
		})
		.unwrap();

		assert!(recalculate_normals(
			&mut packed.bytes,
			48,
			&[0, 1, 2, 0, 3, 1]
		));

		let shared = read(&packed.bytes, NORMAL_OFFSET, 3);
		let length = (shared[0] * shared[0] + shared[1] * shared[1] + shared[2] * shared[2]).sqrt();

		assert!((length - 1.0).abs() < 1e-5);
	}

	#[test]
	fn indices_past_the_vertices_are_rejected()
	{
		let mut bytes = vec![0u8; 48];

		assert!(!recalculate_normals(&mut bytes, 48, &[0, 1, 2]));
	}
}
