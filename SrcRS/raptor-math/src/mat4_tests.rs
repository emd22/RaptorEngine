/*
 * File:        mat4_tests.rs
 * Author:      emd22
 * Created:     04/10/2026
 * Description: Tests for the platform 4x4 matrix, run against whichever SIMD backend is built
 */

mod tests
{
	use crate::mat4f_platform as mp;
	use crate::mat4::{self, Quat};
	use crate::vec3f_platform as v3;
	use crate::vec4f_platform as v4;

	fn rows(m: mp::FLOAT44) -> [f32; 16]
	{
		mp::to_rows(m)
	}

	fn close(a: &[f32; 16], b: &[f32; 16], tolerance: f32) -> bool
	{
		a.iter().zip(b).all(|(a, b)| (a - b).abs() <= tolerance)
	}

	fn sample() -> [f32; 16]
	{
		mat4::flatten(&mat4::box_matrix(
			[1.0, -2.0, 3.0],
			[2.0, 3.0, 0.5],
			Quat {
				x: 0.1,
				y: 0.2,
				z: 0.3,
				w: 0.9,
			},
		))
	}

	#[test]
	fn multiply_matches_the_scalar_version_bit_for_bit()
	{
		let a = sample();
		let b = mat4::flatten(&mat4::rotation(Quat {
			x: 0.5,
			y: -0.5,
			z: 0.5,
			w: 0.5,
		}));

		let neon = rows(mp::mul(mp::from_rows(&a), mp::from_rows(&b)));
		let scalar = mat4::flatten(&mat4::multiply(&mat4::unflatten(&a), &mat4::unflatten(&b)));

		assert_eq!(neon, scalar);
	}

	#[test]
	fn builders_match_the_scalar_versions()
	{
		let quat = Quat {
			x: 0.1,
			y: 0.2,
			z: 0.3,
			w: 0.9,
		};

		assert_eq!(
			rows(mp::rotation(quat.x, quat.y, quat.z, quat.w)),
			mat4::flatten(&mat4::rotation(quat))
		);
		assert_eq!(
			rows(mp::translation([1.0, 2.0, 3.0])),
			mat4::flatten(&mat4::translation([1.0, 2.0, 3.0]))
		);
		assert_eq!(
			rows(mp::scale([1.0, 2.0, 3.0])),
			mat4::flatten(&mat4::scale([1.0, 2.0, 3.0]))
		);
		assert_eq!(
			rows(mp::orthographic(800.0, 600.0, 0.1, 10.0)),
			mat4::flatten(&mat4::orthographic(800.0, 600.0, 0.1, 10.0))
		);
		assert_eq!(rows(mp::identity()), mat4::flatten(&mat4::IDENTITY));
	}

	#[test]
	fn axis_rotations_turn_row_vectors_the_way_the_cpp_ones_do()
	{
		let quarter = std::f32::consts::FRAC_PI_2;

		let x = v4::get_values(mp::mul_vec4(mp::rotate_x(quarter), v4::set(0.0, 1.0, 0.0, 0.0)));
		let y = v4::get_values(mp::mul_vec4(mp::rotate_y(quarter), v4::set(0.0, 0.0, 1.0, 0.0)));
		let z = v4::get_values(mp::mul_vec4(mp::rotate_z(quarter), v4::set(1.0, 0.0, 0.0, 0.0)));

		assert!(close(&pad(x), &pad([0.0, 0.0, 1.0, 0.0]), 1e-6), "{x:?}");
		assert!(close(&pad(y), &pad([1.0, 0.0, 0.0, 0.0]), 1e-6), "{y:?}");
		assert!(close(&pad(z), &pad([0.0, 1.0, 0.0, 0.0]), 1e-6), "{z:?}");
	}

	fn pad(v: [f32; 4]) -> [f32; 16]
	{
		let mut out = [0.0; 16];
		out[..4].copy_from_slice(&v);
		out
	}

	#[test]
	fn axis_rotations_agree_with_the_quaternion()
	{
		let angle = 0.7f32;
		let half = (angle * 0.5).sin_cos();

		assert!(close(
			&rows(mp::rotate_x(angle)),
			&rows(mp::rotation(half.0, 0.0, 0.0, half.1)),
			1e-6
		));
		assert!(close(
			&rows(mp::rotate_y(angle)),
			&rows(mp::rotation(0.0, half.0, 0.0, half.1)),
			1e-6
		));
		assert!(close(
			&rows(mp::rotate_z(angle)),
			&rows(mp::rotation(0.0, 0.0, half.0, half.1)),
			1e-6
		));
	}

	#[test]
	fn transpose_twice_is_the_original_and_columns_round_trip()
	{
		let m = sample();

		assert_eq!(rows(mp::transpose(mp::transpose(mp::from_rows(&m)))), m);
		assert_eq!(rows(mp::from_columns(&rows(mp::transpose(mp::from_rows(&m))))), m);

		let t = rows(mp::transpose(mp::from_rows(&m)));
		for r in 0..4 {
			for c in 0..4 {
				assert_eq!(t[r * 4 + c], m[c * 4 + r]);
			}
		}
	}

	#[test]
	fn transpose_mat3_drops_the_translation()
	{
		let m = sample();
		let t = rows(mp::transpose_mat3(mp::from_rows(&m)));

		for r in 0..3 {
			for c in 0..3 {
				assert_eq!(t[r * 4 + c], m[c * 4 + r]);
			}
			assert_eq!(t[r * 4 + 3], 0.0);
		}
		assert_eq!(&t[12..], &[0.0; 4]);
	}

	#[test]
	fn the_inverse_undoes_the_matrix()
	{
		let m = mp::from_rows(&sample());
		let product = rows(mp::mul(m, mp::inverse(m)));

		assert!(close(&product, &rows(mp::identity()), 1e-5), "{product:?}");
	}

	#[test]
	fn without_translation_clears_the_last_row()
	{
		let m = rows(mp::without_translation(mp::from_rows(&sample())));

		assert_eq!(&m[12..], &[0.0, 0.0, 0.0, 1.0]);
		assert_eq!(&m[..12], &sample()[..12]);
	}

	#[test]
	fn look_at_puts_the_eye_at_the_origin_and_the_target_down_z()
	{
		let eye = v3::set(1.0, 2.0, 3.0);
		let target = v3::set(1.0, 2.0, 8.0);
		let m = mp::look_at(eye, target, v3::set(0.0, 1.0, 0.0));

		let at_eye = v4::get_values(mp::mul_vec4(m, v4::set(1.0, 2.0, 3.0, 1.0)));
		let at_target = v4::get_values(mp::mul_vec4(m, v4::set(1.0, 2.0, 8.0, 1.0)));

		assert!(close(&pad(at_eye), &pad([0.0, 0.0, 0.0, 1.0]), 1e-6), "{at_eye:?}");
		assert!(close(&pad(at_target), &pad([0.0, 0.0, 5.0, 1.0]), 1e-6), "{at_target:?}");
	}

	#[test]
	fn perspective_maps_the_distances_to_window_depth()
	{
		let m = mp::perspective(1.0, 16.0 / 9.0, 1000.0, 0.01);

		let near = v4::get_values(mp::mul_vec4(m, v4::set(0.0, 0.0, 0.01, 1.0)));
		let far = v4::get_values(mp::mul_vec4(m, v4::set(0.0, 0.0, 1000.0, 1.0)));

		assert!((near[2] / near[3]).abs() < 1e-4, "{near:?}");
		assert!((far[2] / far[3] - 1.0).abs() < 1e-4, "{far:?}");
	}

	#[test]
	fn copy_as_mat3_is_the_first_twelve_floats()
	{
		let m = sample();

		assert_eq!(mp::copy_as_mat3(mp::from_rows(&m)), m[..12]);
	}
}
