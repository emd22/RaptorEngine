pub type Quat = [f32; 4];

pub const QUAT_IDENTITY: Quat = [0.0, 0.0, 0.0, 1.0];

/// A row-major 4x4 matrix that transforms row vectors: `v * M`. It is laid out like the engine's
/// `Mat4f`, so the two can share memory.
#[derive(Clone, Copy, Debug, PartialEq)]
#[repr(C, align(16))]
pub struct Mat4(pub [[f32; 4]; 4]);

impl Mat4
{
	pub const IDENTITY: Self = Self([
		[1.0, 0.0, 0.0, 0.0],
		[0.0, 1.0, 0.0, 0.0],
		[0.0, 0.0, 1.0, 0.0],
		[0.0, 0.0, 0.0, 1.0],
	]);

	pub fn translation(position: [f32; 3]) -> Self
	{
		let mut matrix = Self::IDENTITY;

		matrix.0[3] = [position[0], position[1], position[2], 1.0];

		matrix
	}

	pub fn scale(scale: [f32; 3]) -> Self
	{
		let mut matrix = Self::IDENTITY;

		for (row, factor) in matrix.0.iter_mut().zip(scale) {
			for value in row.iter_mut() {
				*value *= factor;
			}
		}

		matrix
	}

	pub fn rotation(quat: Quat) -> Self
	{
		let [x, y, z, w] = quat;

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

		Self([
			[(1.0 - yy) - zz, xy + zw, xz - yw, 0.0],
			[xy - zw, (1.0 - zz) - xx, yz + xw, 0.0],
			[xz + yw, yz - xw, (1.0 - xx) - yy, 0.0],
			[0.0, 0.0, 0.0, 1.0],
		])
	}

	pub fn translation_part(&self) -> [f32; 3]
	{
		[self.0[3][0], self.0[3][1], self.0[3][2]]
	}

	pub fn words(&self) -> impl Iterator<Item = u32> + '_
	{
		self.0.iter().flatten().map(|value| value.to_bits())
	}
}

/// `a * b`, which applies `a` first when transforming row vectors. Each row accumulates with fused
/// multiply-adds in the order the SIMD code does, so the results match it bit for bit.
pub fn multiply(a: &Mat4, b: &Mat4) -> Mat4
{
	let mut result = Mat4([[0.0; 4]; 4]);

	for (row, out) in a.0.iter().zip(&mut result.0) {
		for (column, value) in out.iter_mut().enumerate() {
			let mut sum = b.0[0][column] * row[0];

			for (b_row, factor) in b.0.iter().zip(row).skip(1) {
				sum = b_row[column].mul_add(*factor, sum);
			}

			*value = sum;
		}
	}

	result
}

pub fn lerp3(a: [f32; 3], b: [f32; 3], t: f32) -> [f32; 3]
{
	[
		a[0] + (b[0] - a[0]) * t,
		a[1] + (b[1] - a[1]) * t,
		a[2] + (b[2] - a[2]) * t,
	]
}

/// The sum of the four lanes, pairwise like the horizontal add of the SIMD code.
fn horizontal_sum(v: [f32; 4]) -> f32
{
	(v[0] + v[1]) + (v[2] + v[3])
}

fn dot4(a: Quat, b: Quat) -> f32
{
	horizontal_sum([a[0] * b[0], a[1] * b[1], a[2] * b[2], a[3] * b[3]])
}

/// Interpolates along the shortest arc from `a` to `b`.
pub fn slerp(a: Quat, b: Quat, t: f32) -> Quat
{
	let mut b = b;
	let mut cos_half_theta = dot4(a, b);

	if cos_half_theta < 0.0 {
		b = [-b[0], -b[1], -b[2], -b[3]];
		cos_half_theta = -cos_half_theta;
	}

	if cos_half_theta >= 1.0 {
		return a;
	}

	let half_theta = cos_half_theta.acos();
	let sin_half_theta = (1.0 - cos_half_theta * cos_half_theta).sqrt();

	if sin_half_theta < 0.001 {
		return [
			b[0].mul_add(0.5, a[0] * 0.5),
			b[1].mul_add(0.5, a[1] * 0.5),
			b[2].mul_add(0.5, a[2] * 0.5),
			b[3].mul_add(0.5, a[3] * 0.5),
		];
	}

	let recip = 1.0 / sin_half_theta;
	let ratio_a = ((1.0 - t) * half_theta).sin() * recip;
	let ratio_b = (t * half_theta).sin() * recip;

	[
		b[0].mul_add(ratio_b, a[0] * ratio_a),
		b[1].mul_add(ratio_b, a[1] * ratio_a),
		b[2].mul_add(ratio_b, a[2] * ratio_a),
		b[3].mul_add(ratio_b, a[3] * ratio_a),
	]
}

/// Half the length of the box with the corners `min` and `max`, measured like the engine's
/// `Vec3f::Length` (the sums are added pairwise).
pub fn half_diagonal(min: [f32; 3], max: [f32; 3]) -> f32
{
	let d = [max[0] - min[0], max[1] - min[1], max[2] - min[2]];

	horizontal_sum([d[0] * d[0], d[1] * d[1], d[2] * d[2], 0.0]).sqrt() * 0.5
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn close(a: Quat, b: Quat) -> bool
	{
		a.iter().zip(&b).all(|(a, b)| (a - b).abs() < 1e-5)
	}

	fn about_z(angle: f32) -> Quat
	{
		[0.0, 0.0, (angle * 0.5).sin(), (angle * 0.5).cos()]
	}

	#[test]
	fn slerp_halfway_between_two_turns_is_the_middle_turn()
	{
		assert!(close(slerp(about_z(0.0), about_z(1.0), 0.5), about_z(0.5)));
	}

	#[test]
	fn slerp_takes_the_short_way_round_when_the_keys_are_in_opposite_hemispheres()
	{
		let a = about_z(0.2);
		let b = about_z(0.6).map(|v| -v);

		assert!(close(slerp(a, b, 0.5), about_z(0.4)));
	}

	#[test]
	fn slerp_of_identical_rotations_returns_the_first()
	{
		let a = about_z(0.7);

		assert_eq!(slerp(a, a, 0.3), a);
	}

	#[test]
	fn slerp_of_nearly_opposite_rotations_blends_instead_of_dividing_by_zero()
	{
		let result = slerp([0.0, 0.0, 0.0, 1.0], [1.0, 0.0, 0.0, 1e-6], 0.5);

		assert!(result.iter().all(|v| v.is_finite()));
	}

	#[test]
	fn the_identity_matrix_changes_nothing()
	{
		let matrix = multiply(
			&Mat4::scale([2.0, 3.0, 4.0]),
			&Mat4::translation([1.0, 2.0, 3.0]),
		);

		assert_eq!(multiply(&Mat4::IDENTITY, &matrix), matrix);
		assert_eq!(multiply(&matrix, &Mat4::IDENTITY), matrix);
	}

	#[test]
	fn a_matrix_is_made_of_scale_then_rotation_then_translation()
	{
		let matrix = multiply(
			&multiply(&Mat4::scale([2.0, 1.0, 1.0]), &Mat4::rotation(about_z(1.0))),
			&Mat4::translation([10.0, 0.0, 0.0]),
		);

		let half_turn = about_z(std::f32::consts::PI);
		let flipped = multiply(
			&Mat4::scale([2.0, 1.0, 1.0]),
			&Mat4::rotation(half_turn),
		);

		assert!((flipped.0[0][0] + 2.0).abs() < 1e-5);
		assert_eq!(matrix.translation_part(), [10.0, 0.0, 0.0]);
	}

	#[test]
	fn the_half_diagonal_of_a_unit_cube_is_half_the_square_root_of_three()
	{
		let value = half_diagonal([0.0; 3], [1.0; 3]);

		assert!((value - 3.0_f32.sqrt() * 0.5).abs() < 1e-6);
	}
}
