/*
 * File:        geometry_tests.rs
 * Author:      emd22
 * Created:     05/10/2026
 * Description: Tests for Mat4f, bounding boxes, rays and the frustum
 */

mod tests
{
	use crate::bounds::{Aabb, Obb, RAY_MISS, Ray, ray_cast, ray_cast_face};
	use crate::frustum::{ALL_PLANES, Frustum, FrustumPlane, SIDE_PLANES, frustum_bounding_box, plane_bit};
	use crate::mat4::Quat;
	use crate::mat4f::Mat4f;
	use crate::vec3f::Vec3f;
	use crate::vec4f::Vec4f;

	fn v(x: f32, y: f32, z: f32) -> Vec3f
	{
		Vec3f::new(x, y, z)
	}

	fn near(a: f32, b: f32) -> bool
	{
		(a - b).abs() < 1e-4 * (1.0 + a.abs().max(b.abs()))
	}

	fn rows_near(a: &Mat4f, b: &Mat4f) -> bool
	{
		a.to_rows().iter().zip(b.to_rows()).all(|(a, b)| near(*a, b))
	}

	fn unit_box() -> Aabb
	{
		Aabb::new(v(-1.0, -1.0, -1.0), v(1.0, 1.0, 1.0))
	}

	/// A camera at `eye` looking along +Z with a reverse-Z perspective projection
	fn camera(eye: Vec3f, near_distance: f32, far_distance: f32) -> Mat4f
	{
		let view = Mat4f::look_at(eye, eye + v(0.0, 0.0, 1.0), v(0.0, 1.0, 0.0));
		let projection = Mat4f::perspective(1.2, 1.0, far_distance, near_distance);

		view * projection
	}

	#[test]
	fn the_identity_changes_nothing()
	{
		let matrix = Mat4f::as_scale(v(2.0, 3.0, 4.0)) * Mat4f::as_translation(v(1.0, 2.0, 3.0));

		assert_eq!(Mat4f::identity() * matrix, matrix);
		assert_eq!(matrix * Mat4f::identity(), matrix);
	}

	#[test]
	fn a_matrix_scales_then_turns_then_moves_a_point()
	{
		let matrix = Mat4f::as_scale(v(2.0, 1.0, 1.0))
			* Mat4f::as_rotation(Quat {
				x: 0.0,
				y: 0.0,
				z: core::f32::consts::FRAC_1_SQRT_2,
				w: core::f32::consts::FRAC_1_SQRT_2,
			})
			* Mat4f::as_translation(v(10.0, 0.0, 0.0));

		let moved = matrix * Vec4f::new(1.0, 0.0, 0.0, 1.0);

		assert!(near(moved.x, 10.0) && near(moved.y, 2.0) && near(moved.z, 0.0) && near(moved.w, 1.0));
		assert_eq!(matrix.translation().to_array(), [10.0, 0.0, 0.0]);
	}

	#[test]
	fn a_matrix_times_its_inverse_is_the_identity()
	{
		let matrix = Mat4f::as_scale(v(2.0, 3.0, 0.5))
			* Mat4f::as_rotation(Quat {
				x: 0.1,
				y: 0.2,
				z: 0.3,
				w: 0.9,
			})
			* Mat4f::as_translation(v(1.0, -2.0, 3.0));

		assert!(rows_near(&(matrix * matrix.inverse()), &Mat4f::identity()));
		assert!(rows_near(&(matrix.inverse() * matrix), &Mat4f::identity()));
	}

	#[test]
	fn transposing_twice_gives_the_matrix_back()
	{
		let matrix = Mat4f::from_rows(&[
			1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0,
		]);

		assert_eq!(matrix.transposed().transposed(), matrix);
		assert_eq!(matrix.transposed().to_rows()[1], 5.0);
		assert_eq!(Mat4f::from_columns(&matrix.to_rows()), matrix.transposed());
	}

	#[test]
	fn transposing_the_upper_block_swaps_its_rows_and_columns()
	{
		let matrix = Mat4f::from_rows(&[
			1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0,
		]);

		let rows = matrix.transpose_mat3().to_rows();

		assert_eq!(&rows[..3], &[1.0, 5.0, 9.0]);
		assert_eq!(&rows[4..7], &[2.0, 6.0, 10.0]);
		assert_eq!(&rows[8..11], &[3.0, 7.0, 11.0]);
	}

	#[test]
	fn the_translation_can_be_taken_off()
	{
		let matrix = Mat4f::as_translation(v(1.0, 2.0, 3.0));

		assert_eq!(matrix.without_translation(), Mat4f::identity());
		assert_eq!(matrix.copy_as_mat3()[8..], [0.0, 0.0, 1.0, 0.0]);
	}

	#[test]
	fn a_view_matrix_puts_the_eye_at_the_origin_and_the_target_ahead()
	{
		let view = Mat4f::look_at(v(1.0, 2.0, 3.0), v(1.0, 2.0, 8.0), v(0.0, 1.0, 0.0));

		let eye = view * Vec4f::new(1.0, 2.0, 3.0, 1.0);
		let target = view * Vec4f::new(1.0, 2.0, 8.0, 1.0);

		assert!(near(eye.x, 0.0) && near(eye.y, 0.0) && near(eye.z, 0.0));
		assert!(near(target.x, 0.0) && near(target.y, 0.0) && near(target.z, 5.0));
	}

	#[test]
	fn the_axis_rotations_follow_the_engine_convention()
	{
		let quarter = core::f32::consts::FRAC_PI_2;

		let x = Mat4f::as_rotation_x(quarter) * Vec4f::new(0.0, 1.0, 0.0, 1.0);
		let y = Mat4f::as_rotation_y(quarter) * Vec4f::new(1.0, 0.0, 0.0, 1.0);
		let z = Mat4f::as_rotation_z(quarter) * Vec4f::new(1.0, 0.0, 0.0, 1.0);

		assert!(near(x.z, 1.0) && near(y.z, -1.0) && near(z.y, 1.0));
	}

	#[test]
	fn an_orthographic_projection_maps_the_box_to_the_clip_volume()
	{
		let projection = Mat4f::orthographic(8.0, 6.0, 1.0, 11.0);

		let corner = projection * Vec4f::new(4.0, 3.0, 11.0, 1.0);

		assert!(near(corner.x, 1.0) && near(corner.y, -1.0) && near(corner.z, 1.0));
	}

	#[test]
	fn a_box_grows_to_hold_another()
	{
		let mut a = Aabb::new(v(0.0, 0.0, 0.0), v(1.0, 1.0, 1.0));
		let b = Aabb::new(v(-2.0, 0.5, 0.5), v(0.5, 3.0, 0.75));

		let sum = a + b;
		let sum_of_references = a + &b;

		a += &b;

		assert_eq!(a, sum);
		assert_eq!(a, sum_of_references);
		assert_eq!(a.min.to_array(), [-2.0, 0.0, 0.0]);
		assert_eq!(a.max.to_array(), [1.0, 3.0, 1.0]);
		assert_eq!(a.size().to_array(), [3.0, 3.0, 1.0]);
	}

	#[test]
	fn a_box_can_be_moved()
	{
		let mut aabb = unit_box();

		aabb.offset_by(v(1.0, 2.0, 3.0));

		assert_eq!(aabb.min.to_array(), [0.0, 1.0, 2.0]);
		assert_eq!(aabb.max.to_array(), [2.0, 3.0, 4.0]);
	}

	#[test]
	fn the_corners_of_a_moved_box_are_numbered_by_axis_bits()
	{
		let obb = Obb::from_local_bounds(&unit_box(), &Mat4f::as_translation(v(10.0, 0.0, 0.0)));

		assert_eq!(obb.corners[0].to_array(), [9.0, -1.0, -1.0]);
		assert_eq!(obb.corners[1].to_array(), [11.0, -1.0, -1.0]);
		assert_eq!(obb.corners[2].to_array(), [9.0, 1.0, -1.0]);
		assert_eq!(obb.corners[4].to_array(), [9.0, -1.0, 1.0]);
		assert_eq!(obb.corners[7].to_array(), [11.0, 1.0, 1.0]);
	}

	#[test]
	fn the_world_box_of_a_turned_box_reaches_further_than_the_box()
	{
		let turn = Mat4f::as_rotation_y(core::f32::consts::FRAC_PI_4);
		let aabb = Obb::from_local_bounds(&unit_box(), &turn).world_aabb();

		let reach = core::f32::consts::SQRT_2;

		assert!(near(aabb.max.x, reach) && near(aabb.max.z, reach) && near(aabb.max.y, 1.0));
		assert!(near(aabb.min.x, -reach) && near(aabb.min.z, -reach) && near(aabb.min.y, -1.0));
	}

	#[test]
	fn a_ray_from_outside_meets_the_near_face()
	{
		let ray = Ray::new(v(-5.0, 0.0, 0.0), v(1.0, 0.0, 0.0));

		let (distance, face) = ray_cast_face(&ray, &unit_box());

		assert_eq!(distance, 4.0);
		assert_eq!(face.to_array(), [-1.0, 0.0, 0.0]);
		assert_eq!(ray_cast(&ray, &unit_box()), 4.0);
	}

	#[test]
	fn the_distance_is_in_lengths_of_the_direction()
	{
		let ray = Ray::new(v(0.0, 8.0, 0.0), v(0.0, -2.0, 0.0));

		let (distance, face) = ray_cast_face(&ray, &unit_box());

		assert_eq!(distance, 3.5);
		assert_eq!(face.to_array(), [0.0, 1.0, 0.0]);
	}

	#[test]
	fn a_ray_from_inside_leaves_through_the_face_it_points_at()
	{
		let ray = Ray::new(v(0.0, 0.0, 0.0), v(0.0, 0.0, 1.0));

		let (distance, face) = ray_cast_face(&ray, &unit_box());

		assert_eq!(distance, 1.0);
		assert_eq!(face.to_array(), [0.0, 0.0, 1.0]);
	}

	#[test]
	fn a_ray_that_misses_gives_the_miss_value_and_no_face()
	{
		let beside = Ray::new(v(-5.0, 3.0, 0.0), v(1.0, 0.0, 0.0));
		let away = Ray::new(v(-5.0, 0.0, 0.0), v(-1.0, 0.0, 0.0));
		let past = Ray::new(v(5.0, 0.0, 0.0), v(1.0, 0.0, 0.0));

		for ray in [beside, away, past] {
			let (distance, face) = ray_cast_face(&ray, &unit_box());

			assert_eq!(distance, RAY_MISS);
			assert_eq!(face.to_array(), [0.0; 3]);
		}
	}

	#[test]
	fn a_ray_along_an_axis_is_checked_against_the_slab_it_stays_in()
	{
		let inside = Ray::new(v(-5.0, 0.5, 0.5), v(1.0, 0.0, 0.0));
		let outside = Ray::new(v(-5.0, 1.5, 0.5), v(1.0, 0.0, 0.0));

		assert_eq!(ray_cast(&inside, &unit_box()), 4.0);
		assert_eq!(ray_cast(&outside, &unit_box()), RAY_MISS);
	}

	#[test]
	fn a_ray_with_no_direction_misses()
	{
		let ray = Ray::new(v(0.0, 0.0, 0.0), v(0.0, 0.0, 0.0));

		assert_eq!(ray_cast(&ray, &unit_box()), RAY_MISS);
	}

	#[test]
	fn what_is_in_front_of_the_camera_is_inside_the_frustum()
	{
		let frustum = Frustum::from_view_projection(&camera(v(0.0, 0.0, 0.0), 0.1, 100.0));

		let ahead = Aabb::new(v(-1.0, -1.0, 9.0), v(1.0, 1.0, 11.0));
		let behind = Aabb::new(v(-1.0, -1.0, -11.0), v(1.0, 1.0, -9.0));
		let far_away = Aabb::new(v(-1.0, -1.0, 200.0), v(1.0, 1.0, 202.0));
		let to_the_side = Aabb::new(v(50.0, -1.0, 9.0), v(52.0, 1.0, 11.0));

		assert!(frustum.intersects_aabb(&ahead, ALL_PLANES));
		assert!(!frustum.intersects_aabb(&behind, ALL_PLANES));
		assert!(!frustum.intersects_aabb(&far_away, ALL_PLANES));
		assert!(!frustum.intersects_aabb(&to_the_side, ALL_PLANES));
	}

	#[test]
	fn a_plane_mask_leaves_planes_out_of_the_test()
	{
		let frustum = Frustum::from_view_projection(&camera(v(0.0, 0.0, 0.0), 0.1, 100.0));

		let far_away = Aabb::new(v(-1.0, -1.0, 200.0), v(1.0, 1.0, 202.0));

		assert!(!frustum.intersects_aabb(&far_away, ALL_PLANES));
		assert!(frustum.intersects_aabb(&far_away, SIDE_PLANES));
		assert!(!frustum.intersects_aabb(&far_away, plane_bit(FrustumPlane::Near)));
		assert!(frustum.intersects_aabb(&far_away, plane_bit(FrustumPlane::Far)));
		assert!(frustum.intersects_aabb(&far_away, 0));
	}

	#[test]
	fn the_planes_named_near_and_far_are_the_other_way_round_from_the_geometry()
	{
		let frustum = Frustum::from_view_projection(&camera(v(0.0, 0.0, 0.0), 0.1, 100.0));

		let too_close = Aabb::new(v(-0.01, -0.01, -5.0), v(0.01, 0.01, -4.0));
		let too_far = Aabb::new(v(-1.0, -1.0, 200.0), v(1.0, 1.0, 202.0));

		assert!(!frustum.intersects_aabb(&too_far, plane_bit(FrustumPlane::Near)));
		assert!(!frustum.intersects_aabb(&too_close, plane_bit(FrustumPlane::Far)));
	}

	#[test]
	fn spheres_are_tested_by_how_far_they_are_from_each_plane()
	{
		let frustum = Frustum::from_view_projection(&camera(v(0.0, 0.0, 0.0), 0.1, 100.0));

		assert!(frustum.intersects_sphere(v(0.0, 0.0, 10.0), 1.0, ALL_PLANES));
		assert!(!frustum.intersects_sphere(v(0.0, 0.0, -10.0), 1.0, ALL_PLANES));
		assert!(frustum.intersects_sphere(v(0.0, 0.0, -0.5), 1.0, ALL_PLANES));
		assert!(frustum.intersects_sphere(v(0.0, 0.0, 101.0), 2.0, ALL_PLANES));
		assert!(!frustum.intersects_sphere(v(0.0, 0.0, 105.0), 2.0, ALL_PLANES));
	}

	#[test]
	fn a_turned_box_is_tested_by_its_corners_and_not_the_box_around_them()
	{
		let frustum = Frustum::from_view_projection(&camera(v(0.0, 0.0, 0.0), 0.1, 100.0));

		let local = Aabb::new(v(-1.0, -1.0, -1.0), v(1.0, 1.0, 1.0));

		let ahead = Obb::from_local_bounds(&local, &Mat4f::as_translation(v(0.0, 0.0, 10.0)));
		let behind = Obb::from_local_bounds(&local, &Mat4f::as_translation(v(0.0, 0.0, -10.0)));

		assert!(frustum.intersects_obb(&ahead, ALL_PLANES));
		assert!(!frustum.intersects_obb(&behind, ALL_PLANES));
	}

	#[test]
	fn the_box_and_corner_tests_agree_for_boxes_lined_up_with_the_axes()
	{
		let frustum = Frustum::from_view_projection(&camera(v(0.0, 0.0, 0.0), 0.1, 100.0));

		for z in [-20.0, -2.0, 0.5, 5.0, 50.0, 99.0, 120.0] {
			for x in [-60.0, -5.0, 0.0, 5.0, 60.0] {
				let aabb = Aabb::new(v(x - 1.0, -1.0, z - 1.0), v(x + 1.0, 1.0, z + 1.0));
				let obb = Obb::from_local_bounds(&aabb, &Mat4f::identity());

				assert_eq!(
					frustum.intersects_aabb(&aabb, ALL_PLANES),
					frustum.intersects_obb(&obb, ALL_PLANES),
					"{x} {z}"
				);
			}
		}
	}

	#[test]
	fn tiles_are_tested_in_two_dimensions()
	{
		let frustum = Frustum::from_view_projection(&camera(v(0.0, 0.0, 0.0), 0.1, 100.0));

		let ahead = Aabb::new(v(-5.0, -1.0, 20.0), v(5.0, 1.0, 30.0));
		let behind = Aabb::new(v(-5.0, -1.0, -30.0), v(5.0, 1.0, -20.0));
		let high_above = Aabb::new(v(-5.0, 500.0, 20.0), v(5.0, 502.0, 30.0));

		assert!(frustum.tile_intersects_aabb(&ahead));
		assert!(!frustum.tile_intersects_aabb(&behind));
		assert!(frustum.tile_intersects_aabb(&high_above));
	}

	#[test]
	fn the_planes_point_into_the_frustum_and_are_normalized()
	{
		let frustum = Frustum::from_view_projection(&camera(v(0.0, 0.0, 0.0), 0.1, 100.0));

		for plane in [
			FrustumPlane::Left,
			FrustumPlane::Right,
			FrustumPlane::Bottom,
			FrustumPlane::Top,
			FrustumPlane::Near,
			FrustumPlane::Far,
		] {
			let value = frustum.plane(plane);

			assert!(near(Vec3f::from(value).length(), 1.0), "{plane:?}");
			assert!(Vec3f::from(value).dot(&v(0.0, 0.0, 10.0)) + value.w > 0.0, "{plane:?}");
		}
	}

	#[test]
	fn the_box_around_a_frustum_holds_what_it_can_see()
	{
		let view_projection = camera(v(0.0, 0.0, 0.0), 0.1, 100.0);

		let aabb = frustum_bounding_box(&view_projection);

		assert!(aabb.max.z > 99.0 && aabb.max.z < 101.0);
		assert!(aabb.min.z > -1.0 && aabb.min.z < 1.0);
		assert!(aabb.max.x > 50.0 && aabb.min.x < -50.0);
		assert!(aabb.max.x == -aabb.min.x || near(aabb.max.x, -aabb.min.x));
	}

	#[test]
	fn the_frustum_follows_the_camera()
	{
		let frustum = Frustum::from_view_projection(&camera(v(100.0, 0.0, 0.0), 0.1, 100.0));

		assert!(frustum.intersects_sphere(v(100.0, 0.0, 10.0), 1.0, ALL_PLANES));
		assert!(!frustum.intersects_sphere(v(0.0, 0.0, 10.0), 1.0, ALL_PLANES));
	}
}
