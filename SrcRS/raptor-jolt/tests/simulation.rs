use raptor_jolt::JoltBackend;
use raptor_physics::{Backend, BodySpec, CharacterSpec, Layer, PartSpec};

fn floor(backend: &mut JoltBackend) -> u32 {
	let shape = backend.make_box([20.0, 0.5, 20.0], 300.0, 0.001).unwrap();

	let body = backend
		.create_body(&BodySpec {
			shape,
			position: [0.0, -0.5, 0.0],
			rotation: [0.0, 0.0, 0.0, 1.0],
			dynamic: false,
			layer: Layer::Static,
			friction: 0.5,
			restitution: 0.0,
		})
		.unwrap();

	backend.release_shape(shape);
	backend.add_body(body, false);

	body
}

#[test]
fn a_dynamic_box_comes_to_rest_on_the_floor() {
	let mut backend = JoltBackend::new().unwrap();

	floor(&mut backend);

	let shape = backend.make_box([0.5, 0.5, 0.5], 300.0, 0.001).unwrap();

	let body = backend
		.create_body(&BodySpec {
			shape,
			position: [0.0, 3.0, 0.0],
			rotation: [0.0, 0.0, 0.0, 1.0],
			dynamic: true,
			layer: Layer::Dynamic,
			friction: 0.5,
			restitution: 0.0,
		})
		.unwrap();

	backend.release_shape(shape);
	backend.add_body(body, true);

	for _ in 0..240 {
		backend.step(1.0 / 60.0, 1);
	}

	let (position, _) = backend.position_rotation(body);

	assert!((position[1] - 0.5).abs() < 0.05, "{position:?}");
	assert!(backend.is_dynamic(body));
}

#[test]
fn rays_hit_the_floor_with_an_upward_normal() {
	let mut backend = JoltBackend::new().unwrap();

	let floor = floor(&mut backend);

	let hit = backend
		.cast_ray([0.0, 5.0, 0.0], [0.0, -10.0, 0.0], None)
		.unwrap();

	assert_eq!(hit.body, floor);
	assert!(hit.point[1].abs() < 1.0e-3);
	assert!(hit.normal[1] > 0.99);

	assert!(
		backend
			.cast_ray([0.0, 5.0, 0.0], [0.0, -10.0, 0.0], Some(floor))
			.is_none()
	);

	assert_eq!(
		backend
			.cast_ray_all([0.0, 5.0, 0.0], [0.0, -10.0, 0.0])
			.len(),
		1
	);
}

#[test]
fn a_character_lands_on_the_floor() {
	let mut backend = JoltBackend::new().unwrap();

	floor(&mut backend);

	let character = backend
		.create_character(&CharacterSpec {
			standing_height: 1.0,
			radius: 0.3,
			mass: 80.0,
			max_strength: 100.0,
			max_slope_angle: 0.8,
		})
		.unwrap();

	backend.character_set_position(character, [0.0, 1.0, 0.0]);

	for _ in 0..120 {
		let mut velocity = backend.character_linear_velocity(character);
		velocity[1] -= 9.81 / 60.0;
		backend.character_set_linear_velocity(character, velocity);
		backend.character_update(character, 1.0 / 60.0, [0.0, -9.81, 0.0], Layer::Dynamic);
	}

	assert!(backend.character_on_ground(character));
	assert!(backend.character_position(character)[1].abs() < 0.1);

	backend.destroy_character(character);
}

#[test]
fn a_ragdoll_falls_and_reports_its_hard_landing() {
	let mut backend = JoltBackend::new().unwrap();

	floor(&mut backend);

	backend.impacts().set_min_speed(0.5);

	let part = |y: f32, parent: Option<u32>| PartSpec {
		position: [0.0, y, 0.0],
		rotation: [0.0, 0.0, 0.0, 1.0],
		shape_offset: [0.0; 3],
		shape_rotation: [0.0, 0.0, 0.0, 1.0],
		half_height: 0.2,
		radius: 0.1,
		parent,
		twist_axis: [0.0, 1.0, 0.0],
		plane_axis: [1.0, 0.0, 0.0],
		swing_angle: 0.5,
		twist_angle: 0.3,
	};

	let ragdoll = backend
		.create_ragdoll(
			&[part(4.0, None), part(3.5, Some(0)), part(3.0, Some(1))],
			7,
			raptor_physics::ragdoll_user_data(7),
		)
		.unwrap();

	backend.ragdoll_activate(ragdoll, [0.0; 3]);

	for _ in 0..180 {
		backend.step(1.0 / 60.0, 1);
	}

	let (position, _) = backend.ragdoll_body_pose(ragdoll, 0);
	assert!(position[1] < 2.0, "{position:?}");

	let impacts = backend.impacts().drain();
	assert!(!impacts.is_empty());
	assert!(impacts.iter().all(|impact| impact.ragdoll_serial == 7));

	backend.destroy_ragdoll(ragdoll);
}
