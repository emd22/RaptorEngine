use std::cell::RefCell;
pub type RxSkeleton = raptor_anim::Skeleton;

use std::ffi::{CString, c_char};

use raptor_jolt::JoltBackend;

use raptor_anim::Skeleton;
use raptor_anim::math::Mat4;
use raptor_physics::character::Character;
use raptor_physics::ragdoll::{CreateOptions, Log as RagdollLog, Ragdoll};

use raptor_physics::{BodyId, BodyProps, CharacterSpec, Motion, PhysicsError, PhysicsWorld};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxCharacterSpec
{
	pub standing_height: f32,
	pub radius: f32,
	pub mass: f32,
	pub max_strength: f32,
	pub max_slope_angle: f32,
}

thread_local! {
	static LAST_ERROR: RefCell<CString> = RefCell::new(CString::default());
}

pub const NO_BODY: u32 = u32::MAX;

pub type RxPhysicsWorld = PhysicsWorld<JoltBackend>;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxBodyProps
{
	pub convex_radius: f32,
	pub friction: f32,
	pub restitution: f32,
	pub density: f32,
}

#[repr(C)]
pub struct RxRayResult
{
	pub body: u32,
	pub point: [f32; 3],
	pub normal: [f32; 3],
}

pub const STATUS_OK: i32 = 0;
pub const STATUS_SHAPE_ERROR: i32 = 1;
pub const STATUS_TOO_FEW_POINTS: i32 = 2;
pub const STATUS_NOT_TRIANGLES: i32 = 3;
pub const STATUS_NO_ROOM: i32 = 4;
pub const STATUS_BODY_EXISTS: i32 = 5;

fn props(raw: &RxBodyProps) -> BodyProps
{
	BodyProps {
		convex_radius: raw.convex_radius,
		friction: raw.friction,
		restitution: raw.restitution,
		density: raw.density,
	}
}

fn motion(dynamic: bool) -> Motion
{
	if dynamic {
		Motion::Dynamic
	} else {
		Motion::Static
	}
}

fn previous(body: u32) -> Option<BodyId>
{
	(body != NO_BODY).then_some(BodyId(body))
}

fn status(error: &PhysicsError) -> i32
{
	match error {
		PhysicsError::Shape(_) => STATUS_SHAPE_ERROR,
		PhysicsError::TooFewPoints(_) => STATUS_TOO_FEW_POINTS,
		PhysicsError::NotTriangles => STATUS_NOT_TRIANGLES,
		PhysicsError::NoRoomForBody => STATUS_NO_ROOM,
		PhysicsError::BodyExists => STATUS_BODY_EXISTS,
	}
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_physics_new() -> *mut RxPhysicsWorld
{
	JoltBackend::new().map_or(std::ptr::null_mut(), |backend| {
		Box::into_raw(Box::new(PhysicsWorld::new(backend)))
	})
}

/// # Safety
///
/// `world` must be null or come from `rx_physics_new` and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_free(world: *mut RxPhysicsWorld)
{
	if !world.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(world) });
	}
}

/// What the shape of the last body that could not be made was wrong with.
///
/// # Safety
///
/// `world` must be live. The string is valid until the next body is made.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_last_error(world: *const RxPhysicsWorld) -> *const c_char
{
	// SAFETY: guaranteed by the caller.
	let backend = unsafe { &*world }.backend();

	LAST_ERROR.with(|slot| {
		let mut slot = slot.borrow_mut();
		*slot = CString::new(backend.last_error()).unwrap_or_default();
		slot.as_ptr()
	})
}

/// Makes a box body, replacing `previous` (or `RX_NO_BODY`) and keeping where it was. Writes the
/// body and the dimensions used.
///
/// # Safety
///
/// `world` and `props` must be live, `dimensions` hold three floats, and the outputs be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_create_box_body(
	world: *mut RxPhysicsWorld,
	previous_body: u32,
	dimensions: *const f32,
	dynamic: bool,
	body_props: *const RxBodyProps,
	out_body: *mut u32,
	out_dimensions: *mut f32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let (world, dimensions, body_props) = unsafe {
		(
			&mut *world,
			std::slice::from_raw_parts(dimensions, 3),
			props(&*body_props),
		)
	};

	match world.create_box_body(
		previous(previous_body),
		[dimensions[0], dimensions[1], dimensions[2]],
		motion(dynamic),
		&body_props,
	) {
		Ok((body, used)) => {
			// SAFETY: guaranteed by the caller.
			unsafe {
				*out_body = body.0;
				std::slice::from_raw_parts_mut(out_dimensions, 3).copy_from_slice(&used);
			}

			STATUS_OK
		}
		Err(error) => status(&error),
	}
}

/// Makes a convex hull body from `count` points of three floats.
///
/// # Safety
///
/// As for `rx_physics_create_box_body`, with `points` holding `count` points.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_create_hull_body(
	world: *mut RxPhysicsWorld,
	previous_body: u32,
	points: *const f32,
	count: usize,
	dynamic: bool,
	body_props: *const RxBodyProps,
	out_body: *mut u32,
	out_dimensions: *mut f32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let (world, points, body_props) = unsafe {
		(
			&mut *world,
			std::slice::from_raw_parts(points.cast::<[f32; 3]>(), count),
			props(&*body_props),
		)
	};

	match world.create_hull_body(
		previous(previous_body),
		points,
		motion(dynamic),
		&body_props,
	) {
		Ok((body, size)) => {
			// SAFETY: guaranteed by the caller.
			unsafe {
				*out_body = body.0;
				std::slice::from_raw_parts_mut(out_dimensions, 3).copy_from_slice(&size);
			}

			STATUS_OK
		}
		Err(error) => status(&error),
	}
}

/// Makes a body shaped like a triangle mesh. `existing` is a body that has to be left alone.
///
/// # Safety
///
/// As for `rx_physics_create_box_body`, with `positions` holding `vertex_count` vertices and
/// `indices` `index_count` values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_create_mesh_body(
	world: *mut RxPhysicsWorld,
	existing: u32,
	positions: *const f32,
	vertex_count: usize,
	indices: *const u32,
	index_count: usize,
	dynamic: bool,
	body_props: *const RxBodyProps,
	out_body: *mut u32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let (world, positions, indices, body_props) = unsafe {
		(
			&mut *world,
			std::slice::from_raw_parts(positions.cast::<[f32; 3]>(), vertex_count),
			std::slice::from_raw_parts(indices, index_count),
			props(&*body_props),
		)
	};

	match world.create_mesh_body(
		previous(existing),
		positions,
		indices,
		motion(dynamic),
		&body_props,
	) {
		Ok(body) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out_body = body.0 };

			STATUS_OK
		}
		Err(error) => status(&error),
	}
}

/// # Safety
///
/// `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_add_to_world(world: *mut RxPhysicsWorld, body: u32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *world }.add_to_world(BodyId(body));
}

/// # Safety
///
/// `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_remove_from_world(world: *mut RxPhysicsWorld, body: u32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *world }.remove_from_world(BodyId(body));
}

/// # Safety
///
/// `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_destroy_body(world: *mut RxPhysicsWorld, body: u32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *world }.destroy_body(BodyId(body));
}

/// # Safety
///
/// `world` must be live, `position` hold three floats and `rotation` four.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_teleport(
	world: *mut RxPhysicsWorld,
	body: u32,
	position: *const f32,
	rotation: *const f32,
)
{
	// SAFETY: guaranteed by the caller.
	let (world, position, rotation) = unsafe {
		(
			&mut *world,
			std::slice::from_raw_parts(position, 3),
			std::slice::from_raw_parts(rotation, 4),
		)
	};

	world.teleport(
		BodyId(body),
		[position[0], position[1], position[2]],
		[rotation[0], rotation[1], rotation[2], rotation[3]],
	);
}

/// # Safety
///
/// `world` must be live, `position` writable for three floats and `rotation` for four.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_position_rotation(
	world: *const RxPhysicsWorld,
	body: u32,
	position: *mut f32,
	rotation: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	let (state, position, rotation) = unsafe {
		(
			(&*world).position_rotation(BodyId(body)),
			std::slice::from_raw_parts_mut(position, 3),
			std::slice::from_raw_parts_mut(rotation, 4),
		)
	};

	position.copy_from_slice(&state.0);
	rotation.copy_from_slice(&state.1);
}

/// Casts a ray of the direction's length, skipping the body `ignore` (or `RX_NO_BODY`). Returns
/// whether it hit something.
///
/// # Safety
///
/// `world` must be live, `origin` and `direction` hold three floats, and `out` be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_raycast(
	world: *const RxPhysicsWorld,
	origin: *const f32,
	direction: *const f32,
	ignore: u32,
	out: *mut RxRayResult,
) -> bool
{
	// SAFETY: guaranteed by the caller.
	let (world, origin, direction) = unsafe {
		(
			&*world,
			std::slice::from_raw_parts(origin, 3),
			std::slice::from_raw_parts(direction, 3),
		)
	};

	let Some(hit) = world.raycast(
		[origin[0], origin[1], origin[2]],
		[direction[0], direction[1], direction[2]],
		previous(ignore),
	) else {
		return false;
	};

	// SAFETY: guaranteed by the caller.
	unsafe {
		out.write(RxRayResult {
			body: hit.body.0,
			point: hit.point,
			normal: hit.normal,
		})
	};

	true
}

/// Writes the bodies a ray meets, the nearest first, up to `capacity`, and returns how many there
/// are.
///
/// # Safety
///
/// `world` must be live, `origin` and `direction` hold three floats, and `out` has room for
/// `capacity`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_raycast_objects(
	world: *const RxPhysicsWorld,
	origin: *const f32,
	direction: *const f32,
	out: *mut u32,
	capacity: usize,
) -> usize
{
	// SAFETY: guaranteed by the caller.
	let (world, origin, direction) = unsafe {
		(
			&*world,
			std::slice::from_raw_parts(origin, 3),
			std::slice::from_raw_parts(direction, 3),
		)
	};

	let hits = world.raycast_objects(
		[origin[0], origin[1], origin[2]],
		[direction[0], direction[1], direction[2]],
	);

	for (slot, hit) in (0..capacity).zip(&hits) {
		// SAFETY: guaranteed by the caller.
		unsafe { *out.add(slot) = hit.0 };
	}

	hits.len()
}

/// # Safety
///
/// `world` must be live, `origin` and `direction` hold three floats, and `out` has room for three.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_raycast_face_of_box(
	world: *const RxPhysicsWorld,
	body: u32,
	origin: *const f32,
	direction: *const f32,
	out: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	let (world, origin, direction, out) = unsafe {
		(
			&*world,
			std::slice::from_raw_parts(origin, 3),
			std::slice::from_raw_parts(direction, 3),
			std::slice::from_raw_parts_mut(out, 3),
		)
	};

	out.copy_from_slice(&world.raycast_face_of_box(
		BodyId(body),
		[origin[0], origin[1], origin[2]],
		[direction[0], direction[1], direction[2]],
	));
}

/// # Safety
///
/// `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_is_dynamic(world: *const RxPhysicsWorld, body: u32) -> bool
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*world }.is_dynamic(BodyId(body))
}

/// # Safety
///
/// `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_is_active(world: *const RxPhysicsWorld, body: u32) -> bool
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*world }.is_active(BodyId(body))
}

/// # Safety
///
/// `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_activate(world: *mut RxPhysicsWorld, body: u32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *world }.activate(BodyId(body));
}

/// # Safety
///
/// `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_deactivate(world: *mut RxPhysicsWorld, body: u32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *world }.deactivate(BodyId(body));
}

/// Wakes a body and gives it an impulse.
///
/// # Safety
///
/// `world` must be live and `impulse` hold three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_push(world: *mut RxPhysicsWorld, body: u32, impulse: *const f32)
{
	// SAFETY: guaranteed by the caller.
	let (world, impulse) = unsafe { (&mut *world, std::slice::from_raw_parts(impulse, 3)) };

	world.push(BodyId(body), [impulse[0], impulse[1], impulse[2]]);
}

/// Turns a world space point into one in the space of the body's centre of mass.
///
/// # Safety
///
/// `world` must be live, `point` hold three floats and `out` have room for three.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_point_to_local(
	world: *const RxPhysicsWorld,
	body: u32,
	point: *const f32,
	out: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	let (world, point, out) = unsafe {
		(
			&*world,
			std::slice::from_raw_parts(point, 3),
			std::slice::from_raw_parts_mut(out, 3),
		)
	};

	out.copy_from_slice(&world.point_to_local(BodyId(body), [point[0], point[1], point[2]]));
}

/// Drags a point of a dynamic body in the world towards `target`. Returns false if the body can not
/// be held, and otherwise writes where the point is.
///
/// # Safety
///
/// `world` must be live, the points hold three floats and `out_held` has room for three.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_hold(
	world: *mut RxPhysicsWorld,
	body: u32,
	local_point: *const f32,
	target: *const f32,
	stiffness: f32,
	max_speed: f32,
	angular_damping: f32,
	out_held: *mut f32,
) -> bool
{
	// SAFETY: guaranteed by the caller.
	let (world, local, target, out) = unsafe {
		(
			&mut *world,
			std::slice::from_raw_parts(local_point, 3),
			std::slice::from_raw_parts(target, 3),
			std::slice::from_raw_parts_mut(out_held, 3),
		)
	};

	let Some(held) = world.hold(
		BodyId(body),
		[local[0], local[1], local[2]],
		[target[0], target[1], target[2]],
		stiffness,
		max_speed,
		angular_damping,
	) else {
		return false;
	};

	out.copy_from_slice(&held);

	true
}

/// Advances the simulation one fixed step unless it is paused.
///
/// # Safety
///
/// `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_update(world: *mut RxPhysicsWorld)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *world }.update();
}

/// # Safety
///
/// `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_optimize(world: *mut RxPhysicsWorld)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *world }.optimize();
}

/// # Safety
///
/// `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_set_paused(world: *mut RxPhysicsWorld, paused: bool)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *world }.set_paused(paused);
}

/// # Safety
///
/// `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_is_paused(world: *const RxPhysicsWorld) -> bool
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*world }.is_paused()
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_physics_time_step() -> f32
{
	raptor_physics::world::TIME_STEP
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxImpact
{
	pub point: [f32; 3],
	pub normal: [f32; 3],
	pub speed: f32,
	pub ragdoll_serial: u32,
}

/// # Safety
///
/// `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_set_min_ragdoll_impact_speed(world: *const RxPhysicsWorld, speed: f32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*world }.backend().impacts().set_min_speed(speed);
}

/// Writes up to `capacity` of the queued impacts and forgets them all. Returns how many were
/// written.
///
/// # Safety
///
/// `world` must be live and `out` have room for `capacity` impacts.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_drain_impacts(
	world: *const RxPhysicsWorld,
	out: *mut RxImpact,
	capacity: usize,
) -> usize
{
	// SAFETY: guaranteed by the caller.
	let impacts = unsafe { &*world }.backend().impacts().drain();

	let written = impacts.len().min(capacity);

	for (index, impact) in impacts.iter().take(written).enumerate() {
		// SAFETY: guaranteed by the caller.
		unsafe {
			out.add(index).write(RxImpact {
				point: impact.point,
				normal: impact.normal,
				speed: impact.speed,
				ragdoll_serial: impact.ragdoll_serial,
			})
		};
	}

	written
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_impacts_ragdoll_user_data(serial: u32) -> u64
{
	raptor_physics::ragdoll_user_data(serial)
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_impacts_ragdoll_serial(user_data: u64) -> u32
{
	raptor_physics::ragdoll_serial(user_data)
}

pub type RxCharacter = Character;
pub type RxRagdoll = Ragdoll;

/// Makes the character the player moves around in, or returns null if the world has no room.
///
/// # Safety
///
/// `world` and `spec` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_character_new(
	world: *mut RxPhysicsWorld,
	spec: *const RxCharacterSpec,
) -> *mut RxCharacter
{
	// SAFETY: guaranteed by the caller.
	let (world, spec) = unsafe { (&mut *world, &*spec) };

	Character::new(
		world,
		&CharacterSpec {
			standing_height: spec.standing_height,
			radius: spec.radius,
			mass: spec.mass,
			max_strength: spec.max_strength,
			max_slope_angle: spec.max_slope_angle,
		},
	)
	.map_or(std::ptr::null_mut(), |character| {
		Box::into_raw(Box::new(character))
	})
}

/// # Safety
///
/// `character` must be null or come from `rx_character_new` and must not be used afterwards, and
/// `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_character_free(character: *mut RxCharacter, world: *mut RxPhysicsWorld)
{
	if character.is_null() {
		return;
	}

	// SAFETY: guaranteed by the caller.
	unsafe { *Box::from_raw(character) }.destroy(unsafe { &mut *world });
}

/// # Safety
///
/// `character` and `world` must be live and `position` hold three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_character_teleport(
	character: *const RxCharacter,
	world: *mut RxPhysicsWorld,
	position: *const f32,
)
{
	// SAFETY: guaranteed by the caller.
	let (character, world, position) = unsafe {
		(
			&*character,
			&mut *world,
			std::slice::from_raw_parts(position, 3),
		)
	};

	character.teleport(world, [position[0], position[1], position[2]]);
}

/// # Safety
///
/// `character` must be live and `movement` hold three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_character_apply_movement(
	character: *mut RxCharacter,
	movement: *const f32,
)
{
	// SAFETY: guaranteed by the caller.
	let (character, movement) =
		unsafe { (&mut *character, std::slice::from_raw_parts(movement, 3)) };

	character.apply_movement([movement[0], movement[1], movement[2]]);
}

/// # Safety
///
/// `character` and `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_character_set_collision_enabled(
	character: *mut RxCharacter,
	world: *mut RxPhysicsWorld,
	enabled: bool,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *character }.set_collision_enabled(unsafe { &mut *world }, enabled);
}

/// # Safety
///
/// `character` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_character_set_gravity_disabled(
	character: *mut RxCharacter,
	disabled: bool,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *character }.disable_gravity = disabled;
}

/// # Safety
///
/// `character` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_character_is_grounded(character: *const RxCharacter) -> bool
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*character }.is_grounded()
}

/// # Safety
///
/// `character` and `world` must be live and `out` have room for three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_character_position(
	character: *const RxCharacter,
	world: *const RxPhysicsWorld,
	out: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	let (position, out) = unsafe {
		(
			(&*character).position(&*world),
			std::slice::from_raw_parts_mut(out, 3),
		)
	};

	out.copy_from_slice(&position);
}

/// # Safety
///
/// `character` and `world` must be live and `out` have room for three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_character_linear_velocity(
	character: *const RxCharacter,
	world: *const RxPhysicsWorld,
	out: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	let (velocity, out) = unsafe {
		(
			(&*character).linear_velocity(&*world),
			std::slice::from_raw_parts_mut(out, 3),
		)
	};

	out.copy_from_slice(&velocity);
}

/// Moves the character for `delta_time` seconds.
///
/// # Safety
///
/// `character` and `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_character_update(
	character: *mut RxCharacter,
	world: *mut RxPhysicsWorld,
	delta_time: f32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *character }.update(unsafe { &mut *world }, delta_time);
}

/// Writes the bodies a ray from the character passes through, up to `capacity`, and returns how
/// many there are.
///
/// # Safety
///
/// `character` and `world` must be live, `direction` hold three floats and `out` have room for
/// `capacity` values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_character_ray_bodies(
	character: *const RxCharacter,
	world: *const RxPhysicsWorld,
	direction: *const f32,
	out: *mut u32,
	capacity: usize,
) -> usize
{
	// SAFETY: guaranteed by the caller.
	let (character, world, direction) = unsafe {
		(
			&*character,
			&*world,
			std::slice::from_raw_parts(direction, 3),
		)
	};

	let bodies = character.ray_bodies(world, [direction[0], direction[1], direction[2]]);

	for (slot, body) in (0..capacity).zip(&bodies) {
		// SAFETY: guaranteed by the caller.
		unsafe { *out.add(slot) = *body };
	}

	bodies.len()
}

struct SinkRagdollLog<'a>
{
	sink: &'a crate::RxLogSink,
	category: i32,
}

impl SinkRagdollLog<'_>
{
	fn send(&self, level: i32, message: &str)
	{
		if let Some(log) = self.sink.log {
			// SAFETY: the host promised `log` accepts a pointer and length pair.
			unsafe {
				log(
					self.sink.user,
					level,
					self.category,
					message.as_ptr().cast(),
					message.len(),
				)
			};
		}
	}
}

impl RagdollLog for SinkRagdollLog<'_>
{
	fn warn(&mut self, message: &str)
	{
		self.send(2, message);
	}

	fn info(&mut self, message: &str)
	{
		self.send(1, message);
	}
}

/// Makes a ragdoll from the bones of a skeleton, posed as it is now, or returns null if it can not
/// be made. The matrices are 16 floats in row order.
///
/// # Safety
///
/// `world`, `skeleton` and `log` must be live and the matrices hold sixteen floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ragdoll_new(
	world: *mut RxPhysicsWorld,
	skeleton: *mut crate::RxSkeleton,
	object_world: *const f32,
	object_world_inverse: *const f32,
	serial: u32,
	user_data: u64,
	log: *const crate::RxLogSink,
	log_category: i32,
) -> *mut RxRagdoll
{
	// SAFETY: guaranteed by the caller.
	let (world, skeleton, object_world, object_world_inverse, sink) = unsafe {
		(
			&mut *world,
			&mut *skeleton.cast::<Skeleton>(),
			Mat4(*object_world.cast::<[[f32; 4]; 4]>()),
			Mat4(*object_world_inverse.cast::<[[f32; 4]; 4]>()),
			&*log,
		)
	};

	let mut log = SinkRagdollLog {
		sink,
		category: log_category,
	};

	Ragdoll::create(
		world,
		skeleton,
		CreateOptions {
			object_world,
			object_world_inverse,
			serial,
			user_data,
			log: &mut log,
		},
	)
	.map_or(std::ptr::null_mut(), |ragdoll| {
		Box::into_raw(Box::new(ragdoll))
	})
}

/// # Safety
///
/// `ragdoll` must be null or come from `rx_ragdoll_new` and must not be used afterwards. `world`
/// must be live, and `skeleton` live or null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ragdoll_free(
	ragdoll: *mut RxRagdoll,
	world: *mut RxPhysicsWorld,
	skeleton: *mut crate::RxSkeleton,
)
{
	if ragdoll.is_null() {
		return;
	}

	// SAFETY: guaranteed by the caller.
	unsafe {
		(*Box::from_raw(ragdoll)).destroy(&mut *world, skeleton.cast::<Skeleton>().as_mut());
	}
}

/// # Safety
///
/// `ragdoll` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ragdoll_serial(ragdoll: *const RxRagdoll) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*ragdoll }.serial()
}

/// # Safety
///
/// Everything must be live and `velocity` hold three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ragdoll_activate(
	ragdoll: *const RxRagdoll,
	world: *mut RxPhysicsWorld,
	skeleton: *mut crate::RxSkeleton,
	velocity: *const f32,
)
{
	// SAFETY: guaranteed by the caller.
	let (ragdoll, world, skeleton, velocity) = unsafe {
		(
			&*ragdoll,
			&mut *world,
			&mut *skeleton.cast::<Skeleton>(),
			std::slice::from_raw_parts(velocity, 3),
		)
	};

	ragdoll.activate(world, skeleton, [velocity[0], velocity[1], velocity[2]]);
}

/// # Safety
///
/// Everything must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ragdoll_write_to_skeleton(
	ragdoll: *mut RxRagdoll,
	world: *const RxPhysicsWorld,
	skeleton: *mut crate::RxSkeleton,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *ragdoll }.write_to_skeleton(unsafe { &*world }, unsafe {
		&mut *skeleton.cast::<Skeleton>()
	});
}

/// Writes the matrix of a box around each body, 16 floats each in row order, up to `capacity`
/// boxes, and returns how many bodies there are.
///
/// # Safety
///
/// `ragdoll` and `world` must be live and `out` have room for `capacity` matrices.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ragdoll_debug_boxes(
	ragdoll: *const RxRagdoll,
	world: *const RxPhysicsWorld,
	out: *mut f32,
	capacity: usize,
) -> usize
{
	// SAFETY: guaranteed by the caller.
	let boxes = unsafe { (&*ragdoll).debug_boxes(&*world) };

	for (index, matrix) in boxes.iter().take(capacity).enumerate() {
		// SAFETY: guaranteed by the caller.
		unsafe {
			std::slice::from_raw_parts_mut(out.add(index * 16), 16)
				.copy_from_slice(matrix.0.as_flattened());
		}
	}

	boxes.len()
}

/// Writes the smallest and largest corner of the box around a body in the world.
///
/// # Safety
///
/// `world` must be live and `out_min` and `out_max` have room for three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_physics_body_bounds(
	world: *const RxPhysicsWorld,
	body: u32,
	out_min: *mut f32,
	out_max: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	let ((min, max), out_min, out_max) = unsafe {
		(
			(&*world).body_bounds(BodyId(body)),
			std::slice::from_raw_parts_mut(out_min, 3),
			std::slice::from_raw_parts_mut(out_max, 3),
		)
	};

	out_min.copy_from_slice(&min);
	out_max.copy_from_slice(&max);
}
