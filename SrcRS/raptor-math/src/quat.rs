/*
 * File:        quat.rs
 * Author:      emd22
 * Created:     04/10/2026
 * Description: Quaternions, built on the platform 4 component vector
 */

pub mod quat_platform
{
	use crate::vec3f_platform as v3;
	use crate::vec4f_platform as v4;

	/// A quaternion stored as `(x, y, z, w)`
	pub type QUAT = v4::FLOAT4;

	#[inline(always)]
	pub fn set(x: f32, y: f32, z: f32, w: f32) -> QUAT
	{
		v4::set(x, y, z, w)
	}

	#[inline(always)]
	pub fn identity() -> QUAT
	{
		set(0.0, 0.0, 0.0, 1.0)
	}

	#[inline(always)]
	pub fn load(values: &[f32]) -> QUAT
	{
		v4::load(values)
	}

	#[inline(always)]
	pub fn get_values(q: QUAT) -> [f32; 4]
	{
		v4::get_values(q)
	}

	#[inline(always)]
	pub fn x(q: QUAT) -> f32
	{
		get_values(q)[0]
	}

	#[inline(always)]
	pub fn y(q: QUAT) -> f32
	{
		get_values(q)[1]
	}

	#[inline(always)]
	pub fn z(q: QUAT) -> f32
	{
		get_values(q)[2]
	}

	#[inline(always)]
	pub fn w(q: QUAT) -> f32
	{
		get_values(q)[3]
	}

	#[inline(always)]
	pub fn from_axis_angle(axis: v3::FLOAT4, angle: f32) -> QUAT
	{
		let (s, c) = (angle * 0.5).sin_cos();

		let axis_w1 = v4::set_w(v3::normalize(axis), 1.0);

		v4::mul(axis_w1, v4::set(s, s, s, c))
	}

	/// Builds a quaternion from rotations about X, Y and Z, held in the lanes of `angles`
	#[inline(always)]
	pub fn from_euler_angles(angles: v3::FLOAT4) -> QUAT
	{
		let [ax, ay, az, _] = v3::get_values(angles);

		let (sx, cx) = (ax * 0.5).sin_cos();
		let (sy, cy) = (ay * 0.5).sin_cos();
		let (sz, cz) = (az * 0.5).sin_cos();

		set(
			cz * sx * cy - sz * cx * sy,
			cz * cx * sy + sz * sx * cy,
			sz * cx * cy - cz * sx * sy,
			cz * cx * cy + sz * sx * sy,
		)
	}

	/// The rotations about X, Y and Z in the first three lanes
	#[inline(always)]
	pub fn get_euler_angles(q: QUAT) -> v3::FLOAT4
	{
		let [x, y, z, w] = get_values(q);

		let y_sq = y * y;

		let t0 = 2.0 * (w * x + y * z);
		let t1 = 1.0 - 2.0 * (x * x + y_sq);

		let t2 = (2.0 * (w * y - z * x)).clamp(-1.0, 1.0);

		let t3 = 2.0 * (w * z + x * y);
		let t4 = 1.0 - 2.0 * (y_sq + z * z);

		v3::set(t0.atan2(t1), t2.asin(), t3.atan2(t4))
	}

	/// The forward direction (+Z) rotated by the quaternion
	#[inline(always)]
	pub fn get_direction(q: QUAT) -> v3::FLOAT4
	{
		let [x, y, z, w] = get_values(q);

		v3::set(
			2.0 * ((x * z) + (y * w)),
			2.0 * ((y * z) - (x * w)),
			1.0 - (2.0 * ((x * x) + (y * y))),
		)
	}

	/// The Hamilton product `l * r`
	#[inline(always)]
	pub fn mul(l: QUAT, r: QUAT) -> QUAT
	{
		let [lx, ly, lz, lw] = get_values(l);
		let [rx, ry, rz, rw] = get_values(r);

		set(
			lw * rx + lx * rw + ly * rz - lz * ry,
			lw * ry - lx * rz + ly * rw + lz * rx,
			lw * rz + lx * ry - ly * rx + lz * rw,
			lw * rw - lx * rx - ly * ry - lz * rz,
		)
	}

	#[inline(always)]
	pub fn dot(a: QUAT, b: QUAT) -> f32
	{
		v4::dot(a, b)
	}

	#[inline(always)]
	pub fn conjugate(q: QUAT) -> QUAT
	{
		v4::flip_signs(q, [false, false, false, true])
	}

	#[inline(always)]
	pub fn normalize(q: QUAT) -> QUAT
	{
		v4::normalize(q)
	}

	#[inline(always)]
	pub fn is_close(a: QUAT, b: QUAT, tolerance: f32) -> bool
	{
		v4::is_close(a, b, tolerance)
	}

	/// Normalized linear interpolation, taking the shorter way round
	#[inline(always)]
	pub fn nlerp(a: QUAT, b: QUAT, time: f32) -> QUAT
	{
		let b = if dot(a, b) < 0.0 { v4::neg(b) } else { b };

		let from_a = v4::muls(a, 1.0 - time);

		normalize(v4::mul_add(v4::splat(time), b, from_a))
	}

	/// Spherical interpolation, taking the shorter way round
	pub fn slerp(a: QUAT, b: QUAT, step: f32) -> QUAT
	{
		let mut b = b;
		let mut cos_half_theta = dot(a, b);

		if cos_half_theta < 0.0 {
			b = v4::neg(b);
			cos_half_theta = -cos_half_theta;
		}

		if cos_half_theta.abs() >= 1.0 {
			return a;
		}

		let half_theta = cos_half_theta.acos();
		let sin_half_theta = (1.0 - cos_half_theta * cos_half_theta).sqrt();

		if sin_half_theta < 0.001 {
			let half = v4::splat(0.5);

			return v4::mul_add(b, half, v4::mul(a, half));
		}

		let recip = 1.0 / sin_half_theta;
		let ratio_a = ((1.0 - step) * half_theta).sin() * recip;
		let ratio_b = (step * half_theta).sin() * recip;

		v4::mul_add(b, v4::splat(ratio_b), v4::muls(a, ratio_a))
	}

	/// Moves `a` towards `b` with exponential decay, as `SmoothInterpolate` does in the C++
	#[inline(always)]
	pub fn smooth_interpolate(a: QUAT, b: QUAT, speed: f32, delta_time: f32) -> QUAT
	{
		nlerp(a, b, 1.0 - (-speed * delta_time).exp())
	}
}

#[cfg(test)]
mod tests
{
	use super::quat_platform as qp;
	use crate::mat4::{self, Quat};
	use crate::mat4f_platform as mp;
	use crate::vec3f_platform as v3;
	use crate::vec4f_platform as v4;

	fn close(a: &[f32], b: &[f32], tolerance: f32) -> bool
	{
		a.iter().zip(b).all(|(a, b)| (a - b).abs() <= tolerance)
	}

	fn sample() -> qp::QUAT
	{
		qp::normalize(qp::set(0.1, 0.2, 0.3, 0.9))
	}

	#[test]
	fn lanes_and_identity()
	{
		let q = qp::set(1.0, 2.0, 3.0, 4.0);

		assert_eq!([qp::x(q), qp::y(q), qp::z(q), qp::w(q)], [1.0, 2.0, 3.0, 4.0]);
		assert_eq!(qp::get_values(qp::identity()), [0.0, 0.0, 0.0, 1.0]);
		assert_eq!(qp::get_values(qp::load(&[1.0, 2.0])), [1.0, 2.0, 0.0, 0.0]);
	}

	#[test]
	fn axis_angle_matches_the_axis_rotation_matrices()
	{
		let angle = 0.7;
		let axes = [(1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)];
		let matrices = [mp::rotate_x(angle), mp::rotate_y(angle), mp::rotate_z(angle)];

		for ((x, y, z), expected) in axes.into_iter().zip(matrices) {
			let q = qp::from_axis_angle(v3::set(x * 3.0, y * 3.0, z * 3.0), angle);
			let [qx, qy, qz, qw] = qp::get_values(q);

			assert!(close(
				&mp::to_rows(mp::rotation(qx, qy, qz, qw)),
				&mp::to_rows(expected),
				1e-6
			));
		}
	}

	#[test]
	fn the_product_with_the_identity_and_the_conjugate()
	{
		let q = sample();

		assert!(qp::is_close(qp::mul(q, qp::identity()), q, 1e-7));
		assert!(qp::is_close(qp::mul(qp::identity(), q), q, 1e-7));
		assert!(qp::is_close(qp::mul(q, qp::conjugate(q)), qp::identity(), 1e-6));
	}

	#[test]
	fn the_product_composes_rotations_in_the_order_the_matrices_do()
	{
		let a = sample();
		let b = qp::from_axis_angle(v3::set(0.0, 1.0, 0.0), 1.1);

		let m = |q: qp::QUAT| {
			let [x, y, z, w] = qp::get_values(q);
			mp::rotation(x, y, z, w)
		};

		let product = mp::to_rows(m(qp::mul(a, b)));
		let a_then_b = mp::to_rows(mp::mul(m(a), m(b)));
		let b_then_a = mp::to_rows(mp::mul(m(b), m(a)));

		let matches_a_then_b = close(&product, &a_then_b, 1e-5);
		let matches_b_then_a = close(&product, &b_then_a, 1e-5);

		assert!(matches_a_then_b ^ matches_b_then_a, "a*b is one of the two orders");
		assert!(matches_b_then_a, "quat a*b is the matrix b*a");
	}

	#[test]
	fn euler_angles_round_trip()
	{
		let angles = v3::set(0.3, -0.4, 0.8);
		let back = v3::get_values(qp::get_euler_angles(qp::from_euler_angles(angles)));

		assert!(close(&back[..3], &[0.3, -0.4, 0.8], 1e-5), "{back:?}");
	}

	#[test]
	fn euler_angles_are_a_unit_quaternion()
	{
		let q = qp::from_euler_angles(v3::set(1.0, 2.0, -0.5));

		assert!((qp::dot(q, q) - 1.0).abs() < 1e-6);
	}

	#[test]
	fn the_direction_is_forward_rotated_by_the_matrix()
	{
		let q = sample();
		let [x, y, z, w] = qp::get_values(q);

		let direction = v3::get_values(qp::get_direction(q));
		let rotated = v4::get_values(mp::mul_vec4(
			mp::rotation(x, y, z, w),
			v4::set(0.0, 0.0, 1.0, 0.0),
		));

		assert!(close(&direction[..3], &rotated[..3], 1e-6), "{direction:?} {rotated:?}");
	}

	#[test]
	fn closeness_uses_every_lane()
	{
		let q = sample();
		let off = qp::set(qp::x(q), qp::y(q), qp::z(q), qp::w(q) + 0.01);

		assert!(qp::is_close(q, q, 0.0));
		assert!(qp::is_close(q, off, 0.02));
		assert!(!qp::is_close(q, off, 0.001));
	}

	#[test]
	fn nlerp_hits_the_ends_and_takes_the_short_way()
	{
		let a = qp::identity();
		let b = qp::from_axis_angle(v3::set(0.0, 0.0, 1.0), 1.0);

		assert!(qp::is_close(qp::nlerp(a, b, 0.0), a, 1e-6));
		assert!(qp::is_close(qp::nlerp(a, b, 1.0), b, 1e-6));

		let negated = qp::set(-qp::x(b), -qp::y(b), -qp::z(b), -qp::w(b));
		assert!(qp::is_close(qp::nlerp(a, negated, 1.0), b, 1e-6));

		let mid = qp::nlerp(a, b, 0.5);
		assert!((qp::dot(mid, mid) - 1.0).abs() < 1e-6);
	}

	#[test]
	fn slerp_hits_the_ends_and_halves_the_angle()
	{
		let a = qp::identity();
		let b = qp::from_axis_angle(v3::set(0.0, 1.0, 0.0), 1.2);
		let half = qp::from_axis_angle(v3::set(0.0, 1.0, 0.0), 0.6);

		assert!(qp::is_close(qp::slerp(a, b, 0.0), a, 1e-6));
		assert!(qp::is_close(qp::slerp(a, b, 1.0), b, 1e-6));
		assert!(qp::is_close(qp::slerp(a, b, 0.5), half, 1e-5));
		assert!(qp::is_close(qp::slerp(a, a, 0.5), a, 1e-7));

		let negated = qp::set(-qp::x(b), -qp::y(b), -qp::z(b), -qp::w(b));
		assert!(qp::is_close(qp::slerp(a, negated, 0.5), half, 1e-5));
	}

	#[test]
	fn smooth_interpolation_approaches_the_destination()
	{
		let a = qp::identity();
		let b = qp::from_axis_angle(v3::set(1.0, 0.0, 0.0), 1.0);

		let mut q = a;
		for _ in 0..200 {
			q = qp::smooth_interpolate(q, b, 10.0, 0.016);
		}

		assert!(qp::is_close(q, b, 1e-3));
	}

	#[test]
	fn the_matrix_module_agrees_with_the_plain_quat_struct()
	{
		let q = sample();
		let [x, y, z, w] = qp::get_values(q);

		assert_eq!(
			mp::to_rows(mp::rotation(x, y, z, w)),
			mat4::flatten(&mat4::rotation(Quat { x, y, z, w }))
		);
	}
}
