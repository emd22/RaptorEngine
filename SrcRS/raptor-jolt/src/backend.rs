use std::ffi::{CString, c_char, c_void};
use std::mem::MaybeUninit;
use std::ptr::{null, null_mut};
use std::sync::Arc;

use oxijolt_sys::*;
use raptor_physics::{
	Backend, BodySpec, CharacterSpec, Impact, ImpactQueue, Layer, PartSpec, RagdollSide, RayHit,
	ShapeHandle, is_ragdoll, ragdoll_serial, ragdoll_side,
};

use crate::filters::{BodyFilter, LayerFilters};
use crate::init::{ensure_initialized, lock_globals};

const MAX_BODIES: u32 = 512;
const MAX_BODY_PAIRS: u32 = 1024;
const MAX_CONTACT_CONSTRAINTS: u32 = 1024;
const TEMP_ALLOCATOR_BYTES: u32 = 10 * 1024 * 1024;
const MAX_JOBS: u32 = 2048;
const MAX_BARRIERS: u32 = 8;
const ERROR_CAPACITY: usize = 256;
const INVALID_BODY: u32 = u32::MAX;
const LAYER_COUNT: u32 = 3;
const WALK_STAIRS_COS_ANGLE: f32 = 0.258_819_04;

fn vec3(value: [f32; 3]) -> JPH_Vec3 {
	JPH_Vec3 {
		x: value[0],
		y: value[1],
		z: value[2],
	}
}

fn array3(value: JPH_Vec3) -> [f32; 3] {
	[value.x, value.y, value.z]
}

fn quat(value: [f32; 4]) -> JPH_Quat {
	JPH_Quat {
		x: value[0],
		y: value[1],
		z: value[2],
		w: value[3],
	}
}

fn array4(value: JPH_Quat) -> [f32; 4] {
	[value.x, value.y, value.z, value.w]
}

fn zero_vec() -> JPH_Vec3 {
	vec3([0.0; 3])
}

fn identity() -> JPH_Quat {
	quat([0.0, 0.0, 0.0, 1.0])
}

fn shape_ptr(handle: ShapeHandle) -> *mut JPH_Shape {
	handle.0 as usize as *mut JPH_Shape
}

fn shape_handle(shape: *mut JPH_Shape) -> ShapeHandle {
	ShapeHandle(shape as usize as u64)
}

struct Tables {
	broad_phase: *mut JPH_BroadPhaseLayerInterface,
	pair_filter: *mut JPH_ObjectLayerPairFilter,
	object_vs_broad_phase: *mut JPH_ObjectVsBroadPhaseLayerFilter,
}

impl Tables {
	fn new() -> Self {
		// SAFETY: Jolt is initialised. The tables are filled within their sizes, and the
		// object versus broad phase table only reads the other two while it is made.
		unsafe {
			let pair_filter = JPH_ObjectLayerPairFilterTable_Create(LAYER_COUNT);

			JPH_ObjectLayerPairFilterTable_EnableCollision(
				pair_filter,
				Layer::Static as u32,
				Layer::Dynamic as u32,
			);
			JPH_ObjectLayerPairFilterTable_EnableCollision(
				pair_filter,
				Layer::Dynamic as u32,
				Layer::Dynamic as u32,
			);

			let broad_phase = JPH_BroadPhaseLayerInterfaceTable_Create(LAYER_COUNT, LAYER_COUNT);

			for layer in 0..LAYER_COUNT {
				JPH_BroadPhaseLayerInterfaceTable_MapObjectToBroadPhaseLayer(
					broad_phase,
					layer,
					layer as u8,
				);
			}

			let object_vs_broad_phase = JPH_ObjectVsBroadPhaseLayerFilterTable_Create(
				broad_phase,
				LAYER_COUNT,
				pair_filter,
				LAYER_COUNT,
			);

			Self {
				broad_phase,
				pair_filter,
				object_vs_broad_phase,
			}
		}
	}
}

struct RagdollSlot {
	ragdoll: *mut JPH_Ragdoll,
	bodies: i32,
}

unsafe extern "C" fn on_contact_added(
	user: *mut c_void,
	body1: *const JPH_Body,
	body2: *const JPH_Body,
	manifold: *const JPH_ContactManifold,
	_settings: *mut JPH_ContactSettings,
) {
	// SAFETY: the user data is the impact queue the backend keeps alive for the system's life.
	let queue = unsafe { &*user.cast::<ImpactQueue>() };

	let (first, second) = (body1.cast_mut(), body2.cast_mut());

	// SAFETY: Jolt passes live bodies for the duration of the callback.
	let (first_data, second_data) =
		unsafe { (JPH_Body_GetUserData(first), JPH_Body_GetUserData(second)) };

	let side = ragdoll_side(first_data, second_data);

	if side == RagdollSide::Neither {
		return;
	}

	// SAFETY: the manifold is live for the duration of the callback.
	if unsafe { JPH_ContactManifold_GetPointCount(manifold) } == 0 {
		return;
	}

	let first_is_ragdoll = side == RagdollSide::First;

	let (ragdoll_body, surface_body) = if first_is_ragdoll {
		(first, second)
	} else {
		(second, first)
	};

	// SAFETY: the bodies are live for the duration of the callback.
	if !unsafe { JPH_Body_IsStatic(surface_body) } {
		return;
	}

	let mut normal = zero_vec();
	let mut point = zero_vec();
	let mut velocity = zero_vec();

	// SAFETY: the manifold and bodies are live, and the outputs are writable locals.
	unsafe {
		JPH_ContactManifold_GetWorldSpaceNormal(manifold, &mut normal);

		if first_is_ragdoll {
			JPH_ContactManifold_GetWorldSpaceContactPointOn2(manifold, 0, &mut point);
		} else {
			JPH_ContactManifold_GetWorldSpaceContactPointOn1(manifold, 0, &mut point);
		}

		JPH_Body_GetPointVelocity(ragdoll_body, &point, &mut velocity);
	}

	let surface_normal = if first_is_ragdoll {
		[-normal.x, -normal.y, -normal.z]
	} else {
		[normal.x, normal.y, normal.z]
	};

	let speed = -(velocity.x * surface_normal[0]
		+ velocity.y * surface_normal[1]
		+ velocity.z * surface_normal[2]);

	// SAFETY: the body is live for the duration of the callback.
	let data = unsafe { JPH_Body_GetUserData(ragdoll_body) };

	queue.offer(Impact {
		point: array3(point),
		normal: surface_normal,
		speed,
		ragdoll_serial: if is_ragdoll(data) {
			ragdoll_serial(data)
		} else {
			0
		},
	});
}

static CONTACT_PROCS: JPH_ContactListener_Procs = JPH_ContactListener_Procs {
	OnContactValidate: None,
	OnContactAdded: Some(on_contact_added),
	OnContactPersisted: None,
	OnContactRemoved: None,
};

unsafe extern "C" fn collect_all_hits(context: *mut c_void, result: *const JPH_RayCastResult) {
	// SAFETY: the context is the vector of the call that started the query, and the result is
	// live for the duration of the callback.
	let (hits, result) = unsafe { (&mut *context.cast::<Vec<(u32, f32)>>(), &*result) };

	hits.push((result.bodyID, result.fraction));
}

unsafe extern "C" fn collect_broad_phase_hits(
	context: *mut c_void,
	result: *const JPH_BroadPhaseCastResult,
) -> f32 {
	// SAFETY: as above.
	let (hits, result) = unsafe { (&mut *context.cast::<Vec<u32>>(), &*result) };

	hits.push(result.bodyID);

	1.0
}

pub struct JoltBackend {
	system: *mut JPH_PhysicsSystem,
	bodies: *mut JPH_BodyInterface,
	locks: *const JPH_BodyLockInterface,
	narrow: *const JPH_NarrowPhaseQuery,
	broad: *const JPH_BroadPhaseQuery,
	temp_allocator: *mut JPH_TempAllocator,
	job_system: *mut JPH_JobSystem,
	listener: *mut JPH_ContactListener,
	filters: LayerFilters,
	impacts: Arc<ImpactQueue>,
	characters: Vec<*mut JPH_CharacterVirtual>,
	ragdolls: Vec<Option<RagdollSlot>>,
	next_character_id: u32,
	last_error: String,
}

// SAFETY: Jolt's interfaces can be used from any one thread at a time, and the backend is only
// used through `&mut` or shared reads that Jolt locks internally.
unsafe impl Send for JoltBackend {}

impl JoltBackend {
	pub fn new() -> Option<Self> {
		if !ensure_initialized() {
			return None;
		}

		let tables = Tables::new();
		let impacts = Arc::new(ImpactQueue::default());

		let threads = std::thread::available_parallelism()
			.map_or(1, |count| count.get().saturating_sub(1).max(1));

		let config = JobSystemThreadPoolConfig {
			maxJobs: MAX_JOBS,
			maxBarriers: MAX_BARRIERS,
			numThreads: threads as i32,
		};

		let settings = JPH_PhysicsSystemSettings {
			maxBodies: MAX_BODIES,
			numBodyMutexes: 0,
			maxBodyPairs: MAX_BODY_PAIRS,
			maxContactConstraints: MAX_CONTACT_CONSTRAINTS,
			_padding: 0,
			broadPhaseLayerInterface: tables.broad_phase,
			objectLayerPairFilter: tables.pair_filter,
			objectVsBroadPhaseLayerFilter: tables.object_vs_broad_phase,
		};

		let globals = lock_globals();

		// SAFETY: Jolt is initialised, the settings point at tables that outlive the system, and
		// the listener's user data is the queue that the backend keeps for as long as the
		// system lives.
		let (system, temp_allocator, job_system, listener) = unsafe {
			let system = JPH_PhysicsSystem_Create(&settings);
			let temp_allocator = JPH_TempAllocator_Create(TEMP_ALLOCATOR_BYTES);
			let job_system = JPH_JobSystemThreadPool_Create(&config);

			JPH_ContactListener_SetProcs(&CONTACT_PROCS);

			let listener = JPH_ContactListener_Create(Arc::as_ptr(&impacts).cast_mut().cast());
			JPH_PhysicsSystem_SetContactListener(system, listener);

			(system, temp_allocator, job_system, listener)
		};

		drop(globals);

		if system.is_null() || temp_allocator.is_null() || job_system.is_null() {
			return None;
		}

		// SAFETY: the system is live.
		let (bodies, locks, narrow, broad) = unsafe {
			(
				JPH_PhysicsSystem_GetBodyInterface(system),
				JPH_PhysicsSystem_GetBodyLockInterface(system),
				JPH_PhysicsSystem_GetNarrowPhaseQuery(system),
				JPH_PhysicsSystem_GetBroadPhaseQuery(system),
			)
		};

		Some(Self {
			system,
			bodies,
			locks,
			narrow,
			broad,
			temp_allocator,
			job_system,
			listener,
			filters: LayerFilters::new(),
			impacts,
			characters: Vec::new(),
			ragdolls: Vec::new(),
			next_character_id: 1,
			last_error: String::new(),
		})
	}

	pub fn impacts(&self) -> &Arc<ImpactQueue> {
		&self.impacts
	}

	pub fn last_error(&self) -> &str {
		&self.last_error
	}

	fn finish_shape(&mut self, settings: *mut JPH_ShapeSettings) -> Result<ShapeHandle, String> {
		let mut error = [0 as c_char; ERROR_CAPACITY];

		// SAFETY: the settings are live and the buffer is as large as stated.
		let shape = unsafe {
			JPH_ShapeSettings_CreateShapeWithError(
				settings,
				error.as_mut_ptr(),
				ERROR_CAPACITY as u32,
			)
		};

		// SAFETY: the settings are ours and are no longer needed.
		unsafe { JPH_ShapeSettings_Destroy(settings) };

		if shape.is_null() {
			let bytes: Vec<u8> = error
				.iter()
				.take_while(|byte| **byte != 0)
				.map(|byte| *byte as u8)
				.collect();

			self.last_error = String::from_utf8_lossy(&bytes).into_owned();

			return Err(self.last_error.clone());
		}

		Ok(shape_handle(shape))
	}

	fn creation_settings(
		&self,
		shape: *const JPH_Shape,
		position: [f32; 3],
		rotation: [f32; 4],
		dynamic: bool,
		layer: u32,
	) -> *mut JPH_BodyCreationSettings {
		let position = vec3(position);
		let rotation = quat(rotation);

		// SAFETY: the shape is live and the vectors are locals.
		unsafe {
			JPH_BodyCreationSettings_Create3(
				shape,
				&position,
				&rotation,
				if dynamic {
					JPH_MotionType_Dynamic
				} else {
					JPH_MotionType_Static
				},
				layer,
			)
		}
	}

	fn lock_bounds(&self, body: u32) -> Option<JPH_AABox> {
		let mut lock = MaybeUninit::<JPH_BodyLockRead>::zeroed();
		let mut bounds = JPH_AABox {
			min: zero_vec(),
			max: zero_vec(),
		};

		// SAFETY: the lock interface is live, the lock is written by the call and released by
		// the matching unlock, and the body is only read while the lock is held.
		unsafe {
			JPH_BodyLockInterface_LockRead(self.locks, body, lock.as_mut_ptr());

			let found = !(*lock.as_ptr()).body.is_null();

			if found {
				JPH_Body_GetWorldSpaceBounds((*lock.as_ptr()).body, &mut bounds);
			}

			JPH_BodyLockInterface_UnlockRead(self.locks, lock.as_mut_ptr());

			found.then_some(bounds)
		}
	}

	fn character(&self, character: u32) -> Option<*mut JPH_CharacterVirtual> {
		self.characters
			.get(character as usize)
			.copied()
			.filter(|pointer| !pointer.is_null())
	}

	fn first_free<T>(slots: &[Option<T>]) -> Option<usize> {
		slots.iter().position(Option::is_none)
	}
}

impl Drop for JoltBackend {
	fn drop(&mut self) {
		for slot in self.ragdolls.drain(..).flatten() {
			// SAFETY: the ragdoll is live, and is removed from the system before it is released.
			unsafe {
				JPH_Ragdoll_RemoveFromPhysicsSystem(slot.ragdoll, true);
				JPH_Ragdoll_Destroy(slot.ragdoll);
			}
		}

		for character in self.characters.drain(..) {
			if !character.is_null() {
				// SAFETY: the character is live and released once.
				unsafe { JPH_CharacterBase_Destroy(character.cast()) };
			}
		}

		let _globals = lock_globals();

		// SAFETY: nothing uses the system any more, and the listener is released after it.
		unsafe {
			JPH_PhysicsSystem_Destroy(self.system);
			JPH_ContactListener_Destroy(self.listener);
			JPH_JobSystem_Destroy(self.job_system);
			JPH_TempAllocator_Destroy(self.temp_allocator);
		}
	}
}

impl Backend for JoltBackend {
	fn make_box(
		&mut self,
		half_extents: [f32; 3],
		density: f32,
		convex_radius: f32,
	) -> Result<ShapeHandle, String> {
		let half = vec3(half_extents);

		// SAFETY: the vector is a local, and the settings are used and released here.
		let settings = unsafe {
			let settings = JPH_BoxShapeSettings_Create(&half, convex_radius);
			JPH_ConvexShapeSettings_SetDensity(settings.cast(), density);
			settings
		};

		self.finish_shape(settings.cast())
	}

	fn make_hull(
		&mut self,
		points: &[[f32; 3]],
		convex_radius: f32,
		density: f32,
	) -> Result<ShapeHandle, String> {
		let points: Vec<JPH_Vec3> = points.iter().copied().map(vec3).collect();

		// SAFETY: the points are live for the call, which copies them.
		let settings = unsafe {
			let settings = JPH_ConvexHullShapeSettings_Create(
				points.as_ptr(),
				points.len() as u32,
				convex_radius,
			);
			JPH_ConvexShapeSettings_SetDensity(settings.cast(), density);
			settings
		};

		self.finish_shape(settings.cast())
	}

	fn make_mesh(
		&mut self,
		positions: &[[f32; 3]],
		triangles: &[[u32; 3]],
	) -> Result<ShapeHandle, String> {
		let vertices: Vec<JPH_Vec3> = positions.iter().copied().map(vec3).collect();

		let indexed: Vec<JPH_IndexedTriangle> = triangles
			.iter()
			.map(|triangle| JPH_IndexedTriangle {
				i1: triangle[0],
				i2: triangle[1],
				i3: triangle[2],
				materialIndex: 0,
				userData: 0,
			})
			.collect();

		// SAFETY: the vertices and triangles are live for the call, which copies them.
		let settings = unsafe {
			JPH_MeshShapeSettings_Create2(
				vertices.as_ptr(),
				vertices.len() as u32,
				indexed.as_ptr(),
				indexed.len() as u32,
			)
		};

		self.finish_shape(settings.cast())
	}

	fn release_shape(&mut self, shape: ShapeHandle) {
		// SAFETY: the handle came from a make function and is released once.
		unsafe { JPH_Shape_Destroy(shape_ptr(shape)) };
	}

	fn create_body(&mut self, spec: &BodySpec) -> Option<u32> {
		let settings = self.creation_settings(
			shape_ptr(spec.shape),
			spec.position,
			spec.rotation,
			spec.dynamic,
			spec.layer as u32,
		);

		// SAFETY: the settings are live until destroyed here, and the interface is live.
		let body = unsafe {
			JPH_BodyCreationSettings_SetFriction(settings, spec.friction);
			JPH_BodyCreationSettings_SetRestitution(settings, spec.restitution);

			let body = JPH_BodyInterface_CreateBody(self.bodies, settings);

			JPH_BodyCreationSettings_Destroy(settings);

			body
		};

		if body.is_null() {
			return None;
		}

		// SAFETY: the body was just created.
		Some(unsafe { JPH_Body_GetID(body) })
	}

	fn add_body(&mut self, body: u32, activate: bool) {
		// SAFETY: the interface is live.
		unsafe {
			JPH_BodyInterface_AddBody(
				self.bodies,
				body,
				if activate {
					JPH_Activation_Activate
				} else {
					JPH_Activation_DontActivate
				},
			)
		};
	}

	fn remove_body(&mut self, body: u32) {
		// SAFETY: the interface is live.
		unsafe { JPH_BodyInterface_RemoveBody(self.bodies, body) };
	}

	fn destroy_body(&mut self, body: u32) {
		// SAFETY: the interface is live.
		unsafe { JPH_BodyInterface_DestroyBody(self.bodies, body) };
	}

	fn set_position_rotation(
		&mut self,
		body: u32,
		position: [f32; 3],
		rotation: [f32; 4],
		activate: bool,
	) {
		let (position, rotation) = (vec3(position), quat(rotation));

		// SAFETY: the interface is live and the values are locals.
		unsafe {
			JPH_BodyInterface_SetPositionAndRotation(
				self.bodies,
				body,
				&position,
				&rotation,
				if activate {
					JPH_Activation_Activate
				} else {
					JPH_Activation_DontActivate
				},
			)
		};
	}

	fn position_rotation(&self, body: u32) -> ([f32; 3], [f32; 4]) {
		let (mut position, mut rotation) = (zero_vec(), identity());

		// SAFETY: the interface is live and the outputs are locals.
		unsafe {
			JPH_BodyInterface_GetPositionAndRotation(
				self.bodies,
				body,
				&mut position,
				&mut rotation,
			)
		};

		(array3(position), array4(rotation))
	}

	fn cast_ray(
		&self,
		origin: [f32; 3],
		direction: [f32; 3],
		ignore: Option<u32>,
	) -> Option<RayHit> {
		let (origin_vec, direction_vec) = (vec3(origin), vec3(direction));
		let filter = ignore.map(BodyFilter::ignoring);

		let mut result = JPH_RayCastResult {
			bodyID: INVALID_BODY,
			fraction: 0.0,
			subShapeID2: 0,
		};

		// SAFETY: the query is live and the arguments are locals.
		let hit = unsafe {
			JPH_NarrowPhaseQuery_CastRay(
				self.narrow,
				&origin_vec,
				&direction_vec,
				&mut result,
				null(),
				null(),
				filter.as_ref().map_or(null(), BodyFilter::as_ptr),
			)
		};

		if !hit {
			return None;
		}

		let point = [
			origin[0] + direction[0] * result.fraction,
			origin[1] + direction[1] * result.fraction,
			origin[2] + direction[2] * result.fraction,
		];

		let normal = self
			.surface_normal(result.bodyID, result.subShapeID2, point)
			.unwrap_or([0.0; 3]);

		Some(RayHit {
			body: result.bodyID,
			point,
			normal,
		})
	}

	fn cast_ray_all(&self, origin: [f32; 3], direction: [f32; 3]) -> Vec<(u32, f32)> {
		let (origin, direction) = (vec3(origin), vec3(direction));

		let settings = JPH_RayCastSettings {
			backFaceModeTriangles: JPH_BackFaceMode_IgnoreBackFaces,
			backFaceModeConvex: JPH_BackFaceMode_IgnoreBackFaces,
			treatConvexAsSolid: true,
		};

		let mut hits: Vec<(u32, f32)> = Vec::new();

		// SAFETY: the query is live, and the callback only pushes to the vector, which outlives
		// the call.
		unsafe {
			JPH_NarrowPhaseQuery_CastRay3(
				self.narrow,
				&origin,
				&direction,
				&settings,
				JPH_CollisionCollectorType_AllHit,
				Some(collect_all_hits),
				(&mut hits as *mut Vec<(u32, f32)>).cast(),
				null(),
				null(),
				null(),
				null(),
			)
		};

		hits
	}

	fn surface_normal_along_ray(
		&self,
		body: u32,
		origin: [f32; 3],
		direction: [f32; 3],
	) -> Option<[f32; 3]> {
		let (origin_vec, direction_vec) = (vec3(origin), vec3(direction));

		let mut result = JPH_RayCastResult {
			bodyID: INVALID_BODY,
			fraction: 0.0,
			subShapeID2: 0,
		};

		// SAFETY: the query is live and the arguments are locals.
		let hit = unsafe {
			JPH_NarrowPhaseQuery_CastRay(
				self.narrow,
				&origin_vec,
				&direction_vec,
				&mut result,
				null(),
				null(),
				null(),
			)
		};

		if !hit {
			return None;
		}

		let point = [
			origin[0] + direction[0] * result.fraction,
			origin[1] + direction[1] * result.fraction,
			origin[2] + direction[2] * result.fraction,
		];

		self.surface_normal(body, result.subShapeID2, point)
	}

	fn step(&mut self, delta_time: f32, collision_steps: u32) {
		// SAFETY: the system, allocator and job system are live.
		unsafe {
			JPH_PhysicsSystem_Update2(
				self.system,
				delta_time,
				collision_steps as i32,
				self.temp_allocator,
				self.job_system,
			)
		};
	}

	fn optimize_broad_phase(&mut self) {
		// SAFETY: the system is live.
		unsafe { JPH_PhysicsSystem_OptimizeBroadPhase(self.system) };
	}

	fn is_dynamic(&self, body: u32) -> bool {
		// SAFETY: the interface is live.
		unsafe { JPH_BodyInterface_GetMotionType(self.bodies, body) == JPH_MotionType_Dynamic }
	}

	fn body_bounds(&self, body: u32) -> ([f32; 3], [f32; 3]) {
		self.lock_bounds(body)
			.map_or(([0.0; 3], [0.0; 3]), |bounds| {
				(array3(bounds.min), array3(bounds.max))
			})
	}

	fn is_added(&self, body: u32) -> bool {
		// SAFETY: the interface is live.
		unsafe { JPH_BodyInterface_IsAdded(self.bodies, body) }
	}

	fn is_active(&self, body: u32) -> bool {
		// SAFETY: the interface is live.
		unsafe { JPH_BodyInterface_IsActive(self.bodies, body) }
	}

	fn activate(&mut self, body: u32) {
		// SAFETY: the interface is live.
		unsafe { JPH_BodyInterface_ActivateBody(self.bodies, body) };
	}

	fn deactivate(&mut self, body: u32) {
		// SAFETY: the interface is live.
		unsafe { JPH_BodyInterface_DeactivateBody(self.bodies, body) };
	}

	fn add_impulse(&mut self, body: u32, impulse: [f32; 3]) {
		let mut impulse = vec3(impulse);

		// SAFETY: the interface is live and the impulse is a local.
		unsafe { JPH_BodyInterface_AddImpulse(self.bodies, body, &mut impulse) };
	}

	fn center_of_mass(&self, body: u32) -> ([f32; 3], [f32; 4]) {
		let (mut position, mut rotation) = (zero_vec(), identity());

		// SAFETY: the interface is live and the outputs are locals.
		unsafe {
			JPH_BodyInterface_GetCenterOfMassPosition(self.bodies, body, &mut position);
			JPH_BodyInterface_GetRotation(self.bodies, body, &mut rotation);
		}

		(array3(position), array4(rotation))
	}

	fn angular_velocity(&self, body: u32) -> [f32; 3] {
		let mut velocity = zero_vec();

		// SAFETY: the interface is live and the output is a local.
		unsafe { JPH_BodyInterface_GetAngularVelocity(self.bodies, body, &mut velocity) };

		array3(velocity)
	}

	fn set_velocities(&mut self, body: u32, linear: [f32; 3], angular: [f32; 3]) {
		let (mut linear, mut angular) = (vec3(linear), vec3(angular));

		// SAFETY: the interface is live and the velocities are locals.
		unsafe {
			JPH_BodyInterface_SetLinearAndAngularVelocity(
				self.bodies,
				body,
				&mut linear,
				&mut angular,
			)
		};
	}

	fn gravity(&self) -> [f32; 3] {
		let mut gravity = zero_vec();

		// SAFETY: the system is live and the output is a local.
		unsafe { JPH_PhysicsSystem_GetGravity(self.system, &mut gravity) };

		array3(gravity)
	}

	fn create_character(&mut self, spec: &CharacterSpec) -> Option<u32> {
		let offset = vec3([0.0, 0.5 * spec.standing_height + spec.radius, 0.0]);
		let identity = identity();

		// SAFETY: the capsule and the wrapping shape are made and released here, the settings
		// are local, and the system is live. The character keeps its own references.
		let character = unsafe {
			let capsule = JPH_CapsuleShape_Create(0.5 * spec.standing_height, spec.radius);
			let shape = JPH_RotatedTranslatedShape_Create(&offset, &identity, capsule.cast());

			let mut settings = MaybeUninit::<JPH_CharacterVirtualSettings>::zeroed();
			JPH_CharacterVirtualSettings_Init(settings.as_mut_ptr());
			let mut settings = settings.assume_init();

			settings.base.maxSlopeAngle = spec.max_slope_angle;
			settings.base.shape = shape.cast();
			settings.base.supportingVolume = JPH_Plane {
				normal: vec3([0.0, 1.0, 0.0]),
				distance: -spec.radius,
			};
			settings.collisionTolerance = 0.01;
			settings.predictiveContactDistance = 0.2;
			settings.maxStrength = spec.max_strength;
			settings.mass = spec.mass;
			settings.backFaceMode = JPH_BackFaceMode_CollideWithBackFaces;
			settings.innerBodyLayer = Layer::Dynamic as u32;
			settings.ID = self.next_character_id;

			let position = zero_vec();
			let character =
				JPH_CharacterVirtual_Create(&settings, &position, &identity, 0, self.system);

			JPH_Shape_Destroy(shape.cast());
			JPH_Shape_Destroy(capsule.cast());

			character
		};

		if character.is_null() {
			return None;
		}

		self.next_character_id += 1;

		let slot = self
			.characters
			.iter()
			.position(|pointer| pointer.is_null())
			.unwrap_or_else(|| {
				self.characters.push(null_mut());
				self.characters.len() - 1
			});

		self.characters[slot] = character;

		Some(slot as u32)
	}

	fn destroy_character(&mut self, character: u32) {
		if let Some(pointer) = self.character(character) {
			// SAFETY: the character is live and released once.
			unsafe { JPH_CharacterBase_Destroy(pointer.cast()) };
			self.characters[character as usize] = null_mut();
		}
	}

	fn character_set_position(&mut self, character: u32, position: [f32; 3]) {
		if let Some(pointer) = self.character(character) {
			let position = vec3(position);

			// SAFETY: the character is live.
			unsafe { JPH_CharacterVirtual_SetPosition(pointer, &position) };
		}
	}

	fn character_position(&self, character: u32) -> [f32; 3] {
		let mut position = zero_vec();

		if let Some(pointer) = self.character(character) {
			// SAFETY: the character is live and the output is a local.
			unsafe { JPH_CharacterVirtual_GetPosition(pointer, &mut position) };
		}

		array3(position)
	}

	fn character_linear_velocity(&self, character: u32) -> [f32; 3] {
		let mut velocity = zero_vec();

		if let Some(pointer) = self.character(character) {
			// SAFETY: the character is live and the output is a local.
			unsafe { JPH_CharacterVirtual_GetLinearVelocity(pointer, &mut velocity) };
		}

		array3(velocity)
	}

	fn character_set_linear_velocity(&mut self, character: u32, velocity: [f32; 3]) {
		if let Some(pointer) = self.character(character) {
			let velocity = vec3(velocity);

			// SAFETY: the character is live.
			unsafe { JPH_CharacterVirtual_SetLinearVelocity(pointer, &velocity) };
		}
	}

	fn character_on_ground(&self, character: u32) -> bool {
		self.character(character).is_some_and(|pointer| {
			// SAFETY: the character is live.
			unsafe { JPH_CharacterBase_GetGroundState(pointer.cast()) == JPH_GroundState_OnGround }
		})
	}

	fn character_up(&self, character: u32) -> [f32; 3] {
		let mut up = vec3([0.0, 1.0, 0.0]);

		if let Some(pointer) = self.character(character) {
			// SAFETY: the character is live and the output is a local.
			unsafe { JPH_CharacterBase_GetUp(pointer.cast(), &mut up) };
		}

		array3(up)
	}

	fn character_update(
		&mut self,
		character: u32,
		delta_time: f32,
		gravity: [f32; 3],
		layer: Layer,
	) {
		let Some(pointer) = self.character(character) else {
			return;
		};

		let gravity = vec3(gravity);

		let settings = JPH_ExtendedUpdateSettings {
			stickToFloorStepDown: vec3([0.0, -0.01, 0.0]),
			walkStairsStepUp: vec3([0.0, 1.0, 0.0]),
			walkStairsMinStepForward: 0.02,
			walkStairsStepForwardTest: 0.1,
			walkStairsCosAngleForwardContact: WALK_STAIRS_COS_ANGLE,
			walkStairsStepDownExtra: zero_vec(),
		};

		// SAFETY: the character, filters and allocator are live, and the other arguments are
		// locals.
		unsafe {
			JPH_CharacterVirtual_ExtendedUpdate2(
				pointer,
				delta_time,
				&gravity,
				&settings,
				self.filters.broad_phase(layer),
				self.filters.object(layer),
				null(),
				null(),
				self.temp_allocator,
			)
		};
	}

	fn character_set_layer(&mut self, character: u32, layer: Layer) {
		let Some(pointer) = self.character(character) else {
			return;
		};

		// SAFETY: the character and interface are live.
		unsafe {
			let inner = JPH_CharacterVirtual_GetInnerBodyID(pointer);

			if inner != INVALID_BODY {
				JPH_BodyInterface_SetObjectLayer(self.bodies, inner, layer as u32);
			}
		}
	}

	fn character_ray_bodies(&self, character: u32, direction: [f32; 3]) -> Vec<u32> {
		let Some(pointer) = self.character(character) else {
			return Vec::new();
		};

		let mut origin = zero_vec();
		let direction = vec3(direction);
		let mut hits: Vec<u32> = Vec::new();

		// SAFETY: the character and query are live, and the callback only pushes to the vector,
		// which outlives the call.
		unsafe {
			JPH_CharacterVirtual_GetPosition(pointer, &mut origin);

			JPH_BroadPhaseQuery_CastRay(
				self.broad,
				&origin,
				&direction,
				Some(collect_broad_phase_hits),
				(&mut hits as *mut Vec<u32>).cast(),
				null(),
				null(),
			)
		};

		hits
	}

	fn create_ragdoll(&mut self, parts: &[PartSpec], serial: u32, user_data: u64) -> Option<u32> {
		let count = parts.len() as u32;

		// SAFETY: every object made here is released before the call returns, apart from the
		// ragdoll, whose bodies hold their own references to shapes, constraints and the group
		// filter. The settings, skeleton and creation settings are only read by the calls that
		// take them.
		let ragdoll = unsafe {
			let skeleton = JPH_Skeleton_Create();
			let settings = JPH_RagdollSettings_Create();
			let table = JPH_GroupFilterTable_Create(count);

			for (index, part) in parts.iter().enumerate() {
				let name = CString::new(format!("part{index}")).unwrap_or_default();
				JPH_Skeleton_AddJoint2(
					skeleton,
					name.as_ptr(),
					part.parent.map_or(-1, |parent| parent as i32),
				);

				if let Some(parent) = part.parent {
					JPH_GroupFilterTable_DisableCollision(table, index as u32, parent);
				}
			}

			JPH_RagdollSettings_SetSkeleton(settings, skeleton);
			JPH_RagdollSettings_ResizeParts(settings, count as i32);

			for (index, part) in parts.iter().enumerate() {
				let capsule = JPH_CapsuleShape_Create(part.half_height, part.radius);
				let offset = vec3(part.shape_offset);
				let shape_rotation = quat(part.shape_rotation);
				let shape =
					JPH_RotatedTranslatedShape_Create(&offset, &shape_rotation, capsule.cast());

				let position = vec3(part.position);
				let rotation = quat(part.rotation);

				let creation = JPH_BodyCreationSettings_Create3(
					shape.cast(),
					&position,
					&rotation,
					JPH_MotionType_Dynamic,
					Layer::Dynamic as u32,
				);

				JPH_BodyCreationSettings_SetFriction(creation, 0.8);
				JPH_BodyCreationSettings_SetRestitution(creation, 0.0);
				JPH_BodyCreationSettings_SetLinearDamping(creation, 0.05);
				JPH_BodyCreationSettings_SetAngularDamping(creation, 0.3);

				let group = JPH_CollisionGroup {
					groupFilter: table.cast(),
					groupID: 0,
					subGroupID: index as u32,
				};

				JPH_BodyCreationSettings_SetCollisionGroup(creation, &group);
				JPH_RagdollSettings_SetPart(settings, index as i32, creation);

				if part.parent.is_some() {
					let mut joint = MaybeUninit::<JPH_SwingTwistConstraintSettings>::zeroed();
					JPH_SwingTwistConstraintSettings_Init(joint.as_mut_ptr());
					let mut joint = joint.assume_init();

					joint.space = JPH_ConstraintSpace_WorldSpace;
					joint.position1 = position;
					joint.position2 = position;
					joint.twistAxis1 = vec3(part.twist_axis);
					joint.twistAxis2 = vec3(part.twist_axis);
					joint.planeAxis1 = vec3(part.plane_axis);
					joint.planeAxis2 = vec3(part.plane_axis);
					joint.normalHalfConeAngle = part.swing_angle;
					joint.planeHalfConeAngle = part.swing_angle;
					joint.twistMinAngle = -part.twist_angle;
					joint.twistMaxAngle = part.twist_angle;
					joint.maxFrictionTorque = 0.5;

					JPH_RagdollSettings_SetPartToParentSwingTwist(settings, index as i32, &joint);
				}

				JPH_BodyCreationSettings_Destroy(creation);
				JPH_Shape_Destroy(shape.cast());
				JPH_Shape_Destroy(capsule.cast());
			}

			JPH_RagdollSettings_Stabilize(settings);
			JPH_RagdollSettings_CalculateBodyIndexToConstraintIndex(settings);
			JPH_RagdollSettings_CalculateConstraintIndexToBodyIdxPair(settings);

			let ragdoll =
				JPH_RagdollSettings_CreateRagdoll(settings, self.system, serial, user_data);

			if !ragdoll.is_null() {
				JPH_Ragdoll_AddToPhysicsSystem(ragdoll, JPH_Activation_DontActivate, true);
			}

			JPH_RagdollSettings_Destroy(settings);
			JPH_Skeleton_Destroy(skeleton);
			JPH_GroupFilter_Destroy(table.cast());

			ragdoll
		};

		if ragdoll.is_null() {
			return None;
		}

		// SAFETY: the ragdoll is live.
		let bodies = unsafe { JPH_Ragdoll_GetBodyCount(ragdoll) };

		let slot = Self::first_free(&self.ragdolls).unwrap_or_else(|| {
			self.ragdolls.push(None);
			self.ragdolls.len() - 1
		});

		self.ragdolls[slot] = Some(RagdollSlot { ragdoll, bodies });

		Some(slot as u32)
	}

	fn destroy_ragdoll(&mut self, ragdoll: u32) {
		if let Some(slot) = self
			.ragdolls
			.get_mut(ragdoll as usize)
			.and_then(Option::take)
		{
			// SAFETY: the ragdoll is live, and is removed from the system before it is released.
			unsafe {
				JPH_Ragdoll_RemoveFromPhysicsSystem(slot.ragdoll, true);
				JPH_Ragdoll_Destroy(slot.ragdoll);
			}
		}
	}

	fn ragdoll_activate(&mut self, ragdoll: u32, velocity: [f32; 3]) {
		let Some(Some(slot)) = self.ragdolls.get(ragdoll as usize) else {
			return;
		};

		let velocity = vec3(velocity);

		// SAFETY: the ragdoll and interface are live.
		unsafe {
			for index in 0..slot.bodies {
				let body = JPH_Ragdoll_GetBodyID(slot.ragdoll, index);
				JPH_BodyInterface_SetLinearVelocity(self.bodies, body, &velocity);
			}

			JPH_Ragdoll_Activate(slot.ragdoll, true);
		}
	}

	fn ragdoll_is_active(&self, ragdoll: u32) -> bool {
		match self.ragdolls.get(ragdoll as usize) {
			// SAFETY: the ragdoll is live.
			Some(Some(slot)) => unsafe { JPH_Ragdoll_IsActive(slot.ragdoll, true) },
			_ => false,
		}
	}

	fn ragdoll_body_pose(&self, ragdoll: u32, index: u32) -> ([f32; 3], [f32; 4]) {
		let Some(Some(slot)) = self.ragdolls.get(ragdoll as usize) else {
			return ([0.0; 3], [0.0, 0.0, 0.0, 1.0]);
		};

		// SAFETY: the ragdoll is live.
		let body = unsafe { JPH_Ragdoll_GetBodyID(slot.ragdoll, index as i32) };

		self.position_rotation(body)
	}
}

impl JoltBackend {
	fn surface_normal(&self, body: u32, sub_shape: u32, point: [f32; 3]) -> Option<[f32; 3]> {
		let mut lock = MaybeUninit::<JPH_BodyLockRead>::zeroed();
		let point = vec3(point);
		let mut normal = zero_vec();

		// SAFETY: the lock interface is live, the lock is written by the call and released by
		// the matching unlock, and the body is only read while the lock is held.
		unsafe {
			JPH_BodyLockInterface_LockRead(self.locks, body, lock.as_mut_ptr());

			let found = !(*lock.as_ptr()).body.is_null();

			if found {
				JPH_Body_GetWorldSpaceSurfaceNormal(
					(*lock.as_ptr()).body,
					sub_shape,
					&point,
					&mut normal,
				);
			}

			JPH_BodyLockInterface_UnlockRead(self.locks, lock.as_mut_ptr());

			found.then_some(array3(normal))
		}
	}
}
