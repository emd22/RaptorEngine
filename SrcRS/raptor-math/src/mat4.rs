/// A row-major 4x4 matrix that transforms row vectors: `v * M`. The translation is in the last row.
pub type Mat4 = [[f32; 4]; 4];

use crate::mat4f_platform::{self as mp};

pub const IDENTITY: Mat4 = [
	[1.0, 0.0, 0.0, 0.0],
	[0.0, 1.0, 0.0, 0.0],
	[0.0, 0.0, 1.0, 0.0],
	[0.0, 0.0, 0.0, 1.0],
];

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Quat
{
	pub x: f32,
	pub y: f32,
	pub z: f32,
	pub w: f32,
}

impl Quat
{
	pub const IDENTITY: Self = Self {
		x: 0.0,
		y: 0.0,
		z: 0.0,
		w: 1.0,
	};
}

pub fn translation(position: [f32; 3]) -> Mat4
{
	let mut matrix = IDENTITY;

	matrix[3] = [position[0], position[1], position[2], 1.0];

	matrix
}

pub fn scale(scale: [f32; 3]) -> Mat4
{
	let mut matrix = IDENTITY;

	for (row, factor) in matrix.iter_mut().zip(scale) {
		for value in row.iter_mut() {
			*value *= factor;
		}
	}

	matrix
}

pub fn rotation(quat: Quat) -> Mat4
{
	let Quat { x, y, z, w } = quat;

	let tx = x * 2.0;
	let ty = y * 2.0;
	let tz = z * 2.0;

	let xx = tx * x;
	let yy = ty * y;
	let zz = tz * z;

	let xy = tx * y;
	let xz = tx * z;
	let xw = tx * w;

	let yz = ty * z;
	let yw = ty * w;
	let zw = tz * w;

	[
		[(1.0 - yy) - zz, xy + zw, xz - yw, 0.0],
		[xy - zw, (1.0 - zz) - xx, yz + xw, 0.0],
		[xz + yw, yz - xw, (1.0 - xx) - yy, 0.0],
		[0.0, 0.0, 0.0, 1.0],
	]
}

/// An orthographic projection of a `width` by `height` area centred on the origin, with +Y pointing
/// down the screen and depth running from `near` at 0 to `far` at 1.
pub fn orthographic(width: f32, height: f32, near: f32, far: f32) -> Mat4
{
	debug_assert!(near > 0.0 && far > 0.0);

	let depth_range = 1.0 / (far - near);

	[
		[2.0 / width, 0.0, 0.0, 0.0],
		[0.0, -2.0 / height, 0.0, 0.0],
		[0.0, 0.0, depth_range, 0.0],
		[0.0, 0.0, -depth_range * near, 1.0],
	]
}

/// `a * b`, which applies `a` first when transforming row vectors. Each row accumulates with fused
/// multiply-adds in the same order the SIMD code does, so the results match it bit for bit.
pub fn multiply(a: &Mat4, b: &Mat4) -> Mat4
{
	let mut result = [[0.0; 4]; 4];

	for (row, out) in a.iter().zip(&mut result) {
		for (column, value) in out.iter_mut().enumerate() {
			let mut sum = b[0][column] * row[0];

			for k in 1..4 {
				sum = b[k][column].mul_add(row[k], sum);
			}

			*value = sum;
		}
	}

	result
}

/// The matrix of a box with `half_extent` placed at `center`, scaled first and then rotated.
pub fn box_matrix(center: [f32; 3], half_extent: [f32; 3], rotation_quat: Quat) -> Mat4
{
	multiply(
		&multiply(&scale(half_extent), &rotation(rotation_quat)),
		&translation(center),
	)
}

pub fn flatten(matrix: &Mat4) -> [f32; 16]
{
	let mut flat = [0.0; 16];

	for (index, value) in flat.iter_mut().enumerate() {
		*value = matrix[index / 4][index % 4];
	}

	flat
}

pub fn unflatten(flat: &[f32; 16]) -> Mat4
{
	let mut matrix = [[0.0; 4]; 4];

	for (index, value) in flat.iter().enumerate() {
		matrix[index / 4][index % 4] = *value;
	}

	matrix
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn transform(point: [f32; 3], matrix: &Mat4) -> [f32; 3]
	{
		let mut out = [0.0; 3];

		for (column, value) in out.iter_mut().enumerate() {
			*value = point[0] * matrix[0][column]
				+ point[1] * matrix[1][column]
				+ point[2] * matrix[2][column]
				+ matrix[3][column];
		}

		out
	}

	fn close(a: [f32; 3], b: [f32; 3]) -> bool
	{
		a.iter().zip(&b).all(|(a, b)| (a - b).abs() < 1e-5)
	}

	#[test]
	fn the_identity_changes_nothing()
	{
		let matrix = box_matrix([1.0, 2.0, 3.0], [1.0, 1.0, 1.0], Quat::IDENTITY);

		assert!(close(transform([4.0, 5.0, 6.0], &matrix), [5.0, 7.0, 9.0]));
		assert_eq!(multiply(&IDENTITY, &matrix), matrix);
		assert_eq!(multiply(&matrix, &IDENTITY), matrix);
	}

	#[test]
	fn a_box_is_scaled_then_rotated_then_moved()
	{
		let quarter_turn_about_z = Quat {
			x: 0.0,
			y: 0.0,
			z: std::f32::consts::FRAC_1_SQRT_2,
			w: std::f32::consts::FRAC_1_SQRT_2,
		};

		let matrix = box_matrix([10.0, 0.0, 0.0], [2.0, 1.0, 1.0], quarter_turn_about_z);

		let moved = transform([1.0, 0.0, 0.0], &matrix);

		assert!(close(moved, [10.0, 2.0, 0.0]), "{moved:?}");
	}

	#[test]
	fn the_orthographic_projection_maps_the_window_edges_to_the_clip_edges()
	{
		let matrix = orthographic(800.0, 600.0, 0.1, 10.0);

		assert!(close(
			transform([400.0, 300.0, 0.1], &matrix),
			[1.0, -1.0, 0.0]
		));
		assert!(close(
			transform([-400.0, -300.0, 10.0], &matrix),
			[-1.0, 1.0, 1.0]
		));
	}

	#[test]
	fn flattening_round_trips()
	{
		let matrix = box_matrix([1.0, 2.0, 3.0], [2.0, 3.0, 4.0], Quat::IDENTITY);

		assert_eq!(unflatten(&flatten(&matrix)), matrix);
		assert_eq!(&flatten(&matrix)[12..], &[1.0, 2.0, 3.0, 1.0]);
	}

	#[test]
	fn multiplication_matches_the_plain_sum_of_products()
	{
		let a = box_matrix([1.0, -2.0, 3.0], [2.0, 3.0, 0.5], Quat::IDENTITY);
		let b = rotation(Quat {
			x: 0.1,
			y: 0.2,
			z: 0.3,
			w: 0.9,
		});

		let product = multiply(&a, &b);

		for row in 0..4 {
			for column in 0..4 {
				let plain: f32 = (0..4).map(|k| a[row][k] * b[k][column]).sum();

				assert!((product[row][column] - plain).abs() < 1e-5);
			}
		}
	}
}
