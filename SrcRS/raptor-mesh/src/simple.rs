use crate::Mesh;
use crate::cube::{FaceOptions, emit_quad};

pub fn wireframe_box() -> Mesh
{
	let mut mesh = Mesh::default();

	for corner in 0..8u32 {
		let sign = |bit: u32| if corner & bit != 0 { 1.0 } else { -1.0 };
		mesh.positions.push([sign(1), sign(2), sign(4)]);
	}

	for corner in 0..8u32 {
		for axis in 0..3 {
			let neighbour = corner | (1 << axis);

			if neighbour != corner {
				mesh.indices.push(corner);
				mesh.indices.push(neighbour);
			}
		}
	}

	mesh
}

pub fn line() -> Mesh
{
	Mesh {
		positions: vec![[0.0; 3], [1.0, 0.0, 0.0]],
		indices: vec![0, 1],
		..Mesh::default()
	}
}

pub fn quad(scale_x: f32, scale_y: f32) -> Mesh
{
	let mut mesh = Mesh::default();

	let top_left = [-scale_x, scale_y, -1.0];
	let top_right = [scale_x, scale_y, -1.0];
	let bottom_left = [-scale_x, -scale_y, -1.0];
	let bottom_right = [scale_x, -scale_y, -1.0];

	emit_quad(
		&mut mesh,
		[top_right, top_left, bottom_left, bottom_right],
		&FaceOptions::default(),
		false,
		false,
	);

	mesh
}
