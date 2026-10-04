use std::collections::HashMap;

use crate::Mesh;
use crate::vec::{Vec3, add, cross, length, normalized};

pub const MAX_ICOSPHERE_RESOLUTION: u32 = 8;

const UV_CELL: [f32; 2] = [1.0 / 11.0, 1.0 / 3.0];

const UV_GRID: [[f32; 2]; 22] = [
	[0.0, 1.0],
	[1.0, 0.0],
	[1.0, 2.0],
	[2.0, 1.0],
	[2.0, 3.0],
	[3.0, 0.0],
	[3.0, 2.0],
	[4.0, 1.0],
	[4.0, 3.0],
	[5.0, 0.0],
	[5.0, 2.0],
	[6.0, 1.0],
	[6.0, 3.0],
	[7.0, 0.0],
	[7.0, 2.0],
	[8.0, 1.0],
	[8.0, 3.0],
	[9.0, 0.0],
	[9.0, 2.0],
	[10.0, 1.0],
	[10.0, 3.0],
	[11.0, 2.0],
];

const INDICES: [u32; 60] = [
	2, 6, 4, 6, 10, 8, 10, 14, 12, 14, 18, 16, 18, 21, 20, 0, 3, 2, 2, 3, 6, 3, 7, 6, 6, 7, 10, 7,
	11, 10, 10, 11, 14, 11, 15, 14, 14, 15, 18, 15, 19, 18, 18, 19, 21, 0, 1, 3, 3, 5, 7, 7, 9, 11,
	11, 13, 15, 15, 17, 19,
];

fn base_vertices() -> [Vec3; 22]
{
	let z = (1.0 + 5.0f32.sqrt()) / 2.0;

	[
		[0.0, -1.0, -z],
		[-1.0, -z, 0.0],
		[z, 0.0, -1.0],
		[1.0, -z, 0.0],
		[1.0, z, 0.0],
		[-1.0, -z, 0.0],
		[z, 0.0, 1.0],
		[0.0, -1.0, z],
		[1.0, z, 0.0],
		[-1.0, -z, 0.0],
		[0.0, 1.0, z],
		[-z, 0.0, 1.0],
		[1.0, z, 0.0],
		[-1.0, -z, 0.0],
		[-1.0, z, 0.0],
		[-z, 0.0, -1.0],
		[1.0, z, 0.0],
		[-1.0, -z, 0.0],
		[0.0, 1.0, -z],
		[0.0, -1.0, -z],
		[1.0, z, 0.0],
		[z, 0.0, -1.0],
	]
}

pub fn icosphere(resolution: u32) -> Mesh
{
	let resolution = resolution.min(MAX_ICOSPHERE_RESOLUTION);

	let mut positions: Vec<Vec3> = base_vertices().to_vec();
	let mut texcoords: Vec<[f32; 2]> = UV_GRID
		.iter()
		.map(|uv| [uv[0] * UV_CELL[0], uv[1] * UV_CELL[1]])
		.collect();
	let mut indices: Vec<u32> = INDICES.to_vec();

	for _ in 0..resolution {
		let mut midpoint_of_edge: HashMap<u64, u32> = HashMap::new();
		let triangle_count = indices.len();

		for t in (0..triangle_count).step_by(3) {
			let mut midpoints = [0u32; 3];

			for e in 0..3 {
				let mut first = indices[t + e];
				let mut second = indices[t + (e + 1) % 3];

				if first > second {
					std::mem::swap(&mut first, &mut second);
				}

				let key = u64::from(first) | (u64::from(second) << 32);

				let midpoint = *midpoint_of_edge.entry(key).or_insert_with(|| {
					let index = positions.len() as u32;

					let (a, b) = (positions[first as usize], positions[second as usize]);
					let sum = add(a, b);
					positions.push([sum[0] / 2.0, sum[1] / 2.0, sum[2] / 2.0]);

					let (ta, tb) = (texcoords[first as usize], texcoords[second as usize]);
					texcoords.push([(ta[0] + tb[0]) / 2.0, (ta[1] + tb[1]) / 2.0]);

					index
				});

				midpoints[e] = midpoint;
			}

			let [mid0, mid1, mid2] = midpoints;
			let (i0, i1, i2) = (indices[t], indices[t + 1], indices[t + 2]);

			indices.extend_from_slice(&[i0, mid0, mid2, i1, mid1, mid0, i2, mid2, mid1]);

			indices[t] = mid0;
			indices[t + 1] = mid1;
			indices[t + 2] = mid2;
		}
	}

	for position in &mut positions {
		*position = normalized(*position);
	}

	let normals = positions.clone();

	let tangents = positions
		.iter()
		.map(|normal| {
			let around_up = cross([0.0, 1.0, 0.0], *normal);
			if length(around_up) > 1.0e-6 {
				normalized(around_up)
			} else {
				[1.0, 0.0, 0.0]
			}
		})
		.collect();

	Mesh {
		positions,
		normals,
		tangents,
		texcoords,
		indices,
	}
}
