use raptor_anim::math::{Mat4, multiply};
use raptor_anim::{NO_ANIMATION, NO_BONE, Skeleton};

use crate::backend::{Backend, PartSpec};
use crate::world::PhysicsWorld;

type Vec3 = [f32; 3];

pub trait Log
{
	fn warn(&mut self, message: &str);
	fn info(&mut self, message: &str);
}

struct BoneDef
{
	bone: &'static str,
	end_bone: &'static str,
	extra_length: f32,
	radius: f32,
	swing_degrees: f32,
	twist_degrees: f32,
}

const fn def(
	bone: &'static str,
	end_bone: &'static str,
	extra_length: f32,
	radius: f32,
	swing_degrees: f32,
	twist_degrees: f32,
) -> BoneDef
{
	BoneDef {
		bone,
		end_bone,
		extra_length,
		radius,
		swing_degrees,
		twist_degrees,
	}
}

const HUMANOID: [BoneDef; 13] = [
	def("root", "spine04", 0.0, 0.11, 0.0, 0.0),
	def("spine03", "neck01", 0.0, 0.13, 25.0, 20.0),
	def("neck01", "head", 0.2, 0.09, 40.0, 45.0),
	def("upperarm01.L", "lowerarm01.L", 0.0, 0.05, 95.0, 50.0),
	def("lowerarm01.L", "wrist.L", 0.0, 0.04, 85.0, 35.0),
	def("upperarm01.R", "lowerarm01.R", 0.0, 0.05, 95.0, 50.0),
	def("lowerarm01.R", "wrist.R", 0.0, 0.04, 85.0, 35.0),
	def("upperleg01.L", "lowerleg01.L", 0.0, 0.075, 70.0, 30.0),
	def("lowerleg01.L", "foot.L", 0.0, 0.055, 60.0, 15.0),
	def("foot.L", "toe1-1.L", 0.0, 0.04, 30.0, 10.0),
	def("upperleg01.R", "lowerleg01.R", 0.0, 0.075, 70.0, 30.0),
	def("lowerleg01.R", "foot.R", 0.0, 0.055, 60.0, 15.0),
	def("foot.R", "toe1-1.R", 0.0, 0.04, 30.0, 10.0),
];

const DEGREES_TO_RADIANS: f32 = 0.017_453_292_5;

fn sub(a: Vec3, b: Vec3) -> Vec3
{
	[a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}

fn add(a: Vec3, b: Vec3) -> Vec3
{
	[a[0] + b[0], a[1] + b[1], a[2] + b[2]]
}

fn scale(a: Vec3, factor: f32) -> Vec3
{
	[a[0] * factor, a[1] * factor, a[2] * factor]
}

fn dot(a: Vec3, b: Vec3) -> f32
{
	a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

fn cross(a: Vec3, b: Vec3) -> Vec3
{
	[
		a[1] * b[2] - a[2] * b[1],
		a[2] * b[0] - a[0] * b[2],
		a[0] * b[1] - a[1] * b[0],
	]
}

fn length(a: Vec3) -> f32
{
	dot(a, a).sqrt()
}

fn normalized(a: Vec3) -> Vec3
{
	scale(a, 1.0 / length(a))
}

/// A unit vector at a right angle to `a`, which is a unit vector.
fn perpendicular(a: Vec3) -> Vec3
{
	if a[0].abs() > a[1].abs() {
		let len = (a[0] * a[0] + a[2] * a[2]).sqrt();

		[a[2] / len, 0.0, -a[0] / len]
	} else {
		let len = (a[1] * a[1] + a[2] * a[2]).sqrt();

		[0.0, a[2] / len, -a[1] / len]
	}
}

fn row(matrix: &Mat4, index: usize) -> Vec3
{
	[matrix.0[index][0], matrix.0[index][1], matrix.0[index][2]]
}

/// The rotation of a bone's world matrix as three unit axes at right angles, which also holds
/// when the matrix has scale or shear.
fn axes_of(matrix: &Mat4) -> [Vec3; 3]
{
	let x = normalized(row(matrix, 0));

	let y = row(matrix, 1);
	let y = normalized(sub(y, scale(x, dot(y, x))));

	[x, y, cross(x, y)]
}

/// The quaternion (x, y, z, w) of a rotation whose columns are `axes`.
fn quat_from_axes(axes: &[Vec3; 3]) -> [f32; 4]
{
	let m = |r: usize, c: usize| axes[c][r];

	let trace = m(0, 0) + m(1, 1) + m(2, 2);

	let quat = if trace > 0.0 {
		let s = (trace + 1.0).sqrt() * 2.0;

		[
			(m(2, 1) - m(1, 2)) / s,
			(m(0, 2) - m(2, 0)) / s,
			(m(1, 0) - m(0, 1)) / s,
			0.25 * s,
		]
	} else if m(0, 0) > m(1, 1) && m(0, 0) > m(2, 2) {
		let s = (1.0 + m(0, 0) - m(1, 1) - m(2, 2)).sqrt() * 2.0;

		[
			0.25 * s,
			(m(0, 1) + m(1, 0)) / s,
			(m(0, 2) + m(2, 0)) / s,
			(m(2, 1) - m(1, 2)) / s,
		]
	} else if m(1, 1) > m(2, 2) {
		let s = (1.0 + m(1, 1) - m(0, 0) - m(2, 2)).sqrt() * 2.0;

		[
			(m(0, 1) + m(1, 0)) / s,
			0.25 * s,
			(m(1, 2) + m(2, 1)) / s,
			(m(0, 2) - m(2, 0)) / s,
		]
	} else {
		let s = (1.0 + m(2, 2) - m(0, 0) - m(1, 1)).sqrt() * 2.0;

		[
			(m(0, 2) + m(2, 0)) / s,
			(m(1, 2) + m(2, 1)) / s,
			0.25 * s,
			(m(1, 0) - m(0, 1)) / s,
		]
	};

	normalize_quat(quat)
}

fn normalize_quat(q: [f32; 4]) -> [f32; 4]
{
	let length = (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]).sqrt();

	q.map(|value| value / length)
}

/// The columns of the rotation a unit quaternion stands for.
fn axes_of_quat(q: [f32; 4]) -> [Vec3; 3]
{
	let [x, y, z, w] = q;

	[
		[
			1.0 - 2.0 * (y * y + z * z),
			2.0 * (x * y + z * w),
			2.0 * (x * z - y * w),
		],
		[
			2.0 * (x * y - z * w),
			1.0 - 2.0 * (x * x + z * z),
			2.0 * (y * z + x * w),
		],
		[
			2.0 * (x * z + y * w),
			2.0 * (y * z - x * w),
			1.0 - 2.0 * (x * x + y * y),
		],
	]
}

fn rotate_axes(axes: &[Vec3; 3], vector: Vec3) -> Vec3
{
	add(
		add(scale(axes[0], vector[0]), scale(axes[1], vector[1])),
		scale(axes[2], vector[2]),
	)
}

/// The shortest rotation that takes the unit vector `from` to `to`.
fn quat_from_to(from: Vec3, to: Vec3) -> [f32; 4]
{
	let w = 1.0 + dot(from, to);

	if w < 1e-6 {
		let axis = normalized(perpendicular(from));

		return [axis[0], axis[1], axis[2], 0.0];
	}

	let axis = cross(from, to);

	normalize_quat([axis[0], axis[1], axis[2], w])
}

fn matrix_from_pose(rotation: [f32; 4], position: Vec3) -> Mat4
{
	let axes = axes_of_quat(rotation);

	Mat4([
		[axes[0][0], axes[0][1], axes[0][2], 0.0],
		[axes[1][0], axes[1][1], axes[1][2], 0.0],
		[axes[2][0], axes[2][1], axes[2][2], 0.0],
		[position[0], position[1], position[2], 1.0],
	])
}

fn normalize_rotation_rows(matrix: &mut Mat4)
{
	for row in matrix.0.iter_mut().take(3) {
		let length = (row[0] * row[0] + row[1] * row[1] + row[2] * row[2]).sqrt();

		if length > 1e-6 {
			row[0] /= length;
			row[1] /= length;
			row[2] /= length;
		}

		row[3] = 0.0;
	}
}

/// What of a ragdoll body is needed to draw it and to pose the bone that it drives.
struct BodyInfo
{
	bone: u32,
	local_axis: Vec3,
	local_center: Vec3,
	length: f32,
	radius: f32,
}

struct ResolvedBone
{
	def: usize,
	bone: u32,
	end_bone: u32,
}

pub struct Ragdoll
{
	handle: u32,
	serial: u32,
	bodies: Vec<BodyInfo>,
	driven_world: Vec<Mat4>,
	is_driven: Vec<u8>,
	world_inverse: Mat4,
	pose_settled: bool,
}

pub struct CreateOptions<'a>
{
	pub object_world: Mat4,
	pub object_world_inverse: Mat4,
	pub serial: u32,
	pub user_data: u64,
	pub log: &'a mut dyn Log,
}

impl Ragdoll
{
	/// Builds a humanoid ragdoll from the bones of a skeleton, posed as it is now, and adds it to
	/// the world. Returns none if the skeleton does not look like a humanoid or the world is
	/// full.
	pub fn create<B: Backend>(
		world: &mut PhysicsWorld<B>,
		skeleton: &mut Skeleton,
		options: CreateOptions,
	) -> Option<Self>
	{
		let CreateOptions {
			object_world,
			object_world_inverse,
			serial,
			user_data,
			log,
		} = options;

		let joint_count = skeleton.joint_count();

		if joint_count == 0 {
			return None;
		}

		let (animation, time) = skeleton
			.active_playback()
			.map_or((NO_ANIMATION, 0.0), |playback| {
				(playback.animation, playback.time)
			});

		skeleton.evaluate_pose(animation, time);

		let object_scale = length(row(&object_world, 0));

		let mut body_of_bone = vec![-1i32; joint_count as usize];
		let mut parts: Vec<ResolvedBone> = Vec::new();

		for (index, def) in HUMANOID.iter().enumerate() {
			let bone = skeleton.find_bone(def.bone.as_bytes());
			let end_bone = skeleton.find_bone(def.end_bone.as_bytes());

			if bone == NO_BONE || end_bone == NO_BONE {
				log.warn(&format!(
					"Ragdoll: skeleton has no bone '{}' or '{}', skipping it",
					def.bone, def.end_bone
				));
				continue;
			}

			body_of_bone[bone as usize] = parts.len() as i32;
			parts.push(ResolvedBone {
				def: index,
				bone,
				end_bone,
			});
		}

		if parts.len() < 2 {
			log.warn("Ragdoll: skeleton does not look like a humanoid, not creating a ragdoll");
			return None;
		}

		let mut specs = Vec::with_capacity(parts.len());
		let mut bodies = Vec::with_capacity(parts.len());

		for (index, part) in parts.iter().enumerate() {
			let def = &HUMANOID[part.def];

			let mut parent_body = -1;
			let mut ancestor = skeleton.parent(part.bone);

			while ancestor != NO_BONE {
				if body_of_bone[ancestor as usize] >= 0 {
					parent_body = body_of_bone[ancestor as usize];
					break;
				}

				ancestor = skeleton.parent(ancestor);
			}

			if parent_body < 0 && index != 0 {
				log.warn(&format!(
					"Ragdoll: bone '{}' has no ragdoll ancestor, not creating a ragdoll",
					def.bone
				));
				return None;
			}

			let bone_world = multiply(
				&skeleton.world_transforms()[part.bone as usize],
				&object_world,
			);
			let axes = axes_of(&bone_world);
			let orientation = quat_from_axes(&axes);
			let position = row(&bone_world, 3);

			let end_world = multiply(
				&skeleton.world_transforms()[part.end_bone as usize],
				&object_world,
			);
			let delta = sub(row(&end_world, 3), position);
			let span = length(delta);

			if span < 1e-4 {
				log.warn(&format!(
					"Ragdoll: bone '{}' has no length, not creating a ragdoll",
					def.bone
				));
				return None;
			}

			let axis_world = scale(delta, 1.0 / span);
			let bone_length = span + def.extra_length * object_scale;
			let radius = def.radius * object_scale;
			let half_height = (bone_length * 0.5 - radius).max(0.01 * object_scale);

			let axis_local = [
				dot(axes[0], axis_world),
				dot(axes[1], axis_world),
				dot(axes[2], axis_world),
			];
			let center_local = scale(axis_local, bone_length * 0.5);

			specs.push(PartSpec {
				position,
				rotation: orientation,
				shape_offset: center_local,
				shape_rotation: quat_from_to([0.0, 1.0, 0.0], axis_local),
				half_height,
				radius,
				parent: (parent_body >= 0).then_some(parent_body as u32),
				twist_axis: axis_world,
				plane_axis: perpendicular(axis_world),
				swing_angle: def.swing_degrees * DEGREES_TO_RADIANS,
				twist_angle: def.twist_degrees * DEGREES_TO_RADIANS,
			});

			bodies.push(BodyInfo {
				bone: part.bone,
				local_axis: axis_local,
				local_center: center_local,
				length: bone_length,
				radius,
			});
		}

		let Some(handle) = world
			.backend_mut()
			.create_ragdoll(&specs, serial, user_data)
		else {
			log.warn("Ragdoll: the physics system is out of bodies");
			return None;
		};

		let mut is_driven = vec![0u8; joint_count as usize];

		for body in &bodies {
			is_driven[body.bone as usize] = 1;
		}

		let mut ragdoll = Self {
			handle,
			serial,
			bodies,
			driven_world: vec![Mat4::IDENTITY; joint_count as usize],
			is_driven,
			world_inverse: object_world_inverse,
			pose_settled: false,
		};

		let expected: Vec<Mat4> = skeleton.skinning_matrices().to_vec();

		ragdoll.write_to_skeleton(world, skeleton);

		let mut worst_error = 0.0f32;

		for (matrix, wanted) in skeleton.skinning_matrices().iter().zip(&expected) {
			for (row, wanted_row) in matrix.0.iter().zip(&wanted.0) {
				for (value, wanted_value) in row.iter().zip(wanted_row) {
					worst_error = worst_error.max((value - wanted_value).abs());
				}
			}
		}

		log.info(&format!(
			"Ragdoll: {} bodies, rest pose skinning error {worst_error:.5}",
			ragdoll.bodies.len()
		));

		Some(ragdoll)
	}

	pub fn serial(&self) -> u32
	{
		self.serial
	}

	/// Lets the bodies go with a velocity and hands the skeleton over to them.
	pub fn activate<B: Backend>(
		&self,
		world: &mut PhysicsWorld<B>,
		skeleton: &mut Skeleton,
		velocity: Vec3,
	)
	{
		world.backend_mut().ragdoll_activate(self.handle, velocity);
		skeleton.set_external_pose(true);
	}

	pub fn destroy<B: Backend>(self, world: &mut PhysicsWorld<B>, skeleton: Option<&mut Skeleton>)
	{
		world.backend_mut().destroy_ragdoll(self.handle);

		if let Some(skeleton) = skeleton {
			skeleton.set_external_pose(false);
		}
	}

	/// Poses the skeleton from where the bodies are, until they have come to rest and the pose
	/// has been written once more.
	pub fn write_to_skeleton<B: Backend>(
		&mut self,
		world: &PhysicsWorld<B>,
		skeleton: &mut Skeleton,
	)
	{
		let asleep = !world.backend().ragdoll_is_active(self.handle);

		if asleep && self.pose_settled {
			return;
		}

		self.pose_settled = asleep;

		for (index, body) in self.bodies.iter().enumerate() {
			let (position, rotation) = world.backend().ragdoll_body_pose(self.handle, index as u32);

			let mut model = multiply(&matrix_from_pose(rotation, position), &self.world_inverse);

			normalize_rotation_rows(&mut model);

			self.driven_world[body.bone as usize] = model;
		}

		skeleton.pose_from_driven_bones(&self.driven_world, &self.is_driven);
	}

	/// The matrix of a box around each body, to draw it with.
	pub fn debug_boxes<B: Backend>(&self, world: &PhysicsWorld<B>) -> Vec<Mat4>
	{
		self.bodies
			.iter()
			.enumerate()
			.map(|(index, body)| {
				let (position, rotation) =
					world.backend().ragdoll_body_pose(self.handle, index as u32);

				let axes = axes_of_quat(rotation);

				let axis = rotate_axes(&axes, body.local_axis);
				let center = add(position, rotate_axes(&axes, body.local_center));
				let side = perpendicular(axis);
				let front = cross(side, axis);

				let x = scale(side, body.radius);
				let y = scale(axis, body.length * 0.5);
				let z = scale(front, body.radius);

				Mat4([
					[x[0], x[1], x[2], 0.0],
					[y[0], y[1], y[2], 0.0],
					[z[0], z[1], z[2], 0.0],
					[center[0], center[1], center[2], 1.0],
				])
			})
			.collect()
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn the_perpendicular_of_an_axis_is_a_unit_vector_at_a_right_angle()
	{
		for axis in [
			[1.0, 0.0, 0.0],
			[0.0, 1.0, 0.0],
			[0.0, 0.0, 1.0],
			normalized([1.0, 2.0, 3.0]),
		] {
			let perp = perpendicular(axis);

			assert!(dot(perp, axis).abs() < 1e-6);
			assert!((length(perp) - 1.0).abs() < 1e-6);
		}
	}

	#[test]
	fn a_rotation_survives_a_round_trip_through_a_quaternion()
	{
		let quat = normalize_quat([0.3, -0.5, 0.2, 0.8]);
		let back = quat_from_axes(&axes_of_quat(quat));

		let same = (0..4).all(|i| (back[i] - quat[i]).abs() < 1e-5)
			|| (0..4).all(|i| (back[i] + quat[i]).abs() < 1e-5);

		assert!(same, "{quat:?} {back:?}");
	}

	#[test]
	fn the_rotation_between_two_directions_takes_one_to_the_other()
	{
		let from = [0.0, 1.0, 0.0];
		let to = normalized([1.0, 1.0, 0.0]);

		let axes = axes_of_quat(quat_from_to(from, to));
		let turned = rotate_axes(&axes, from);

		for axis in 0..3 {
			assert!((turned[axis] - to[axis]).abs() < 1e-5);
		}

		let flipped = rotate_axes(&axes_of_quat(quat_from_to(from, [0.0, -1.0, 0.0])), from);

		assert!((flipped[1] + 1.0).abs() < 1e-5);
	}

	#[test]
	fn a_bone_matrix_with_scale_still_gives_right_angled_unit_axes()
	{
		let matrix = Mat4([
			[2.0, 0.0, 0.0, 0.0],
			[0.1, 3.0, 0.0, 0.0],
			[0.0, 0.0, 4.0, 0.0],
			[5.0, 6.0, 7.0, 1.0],
		]);

		let axes = axes_of(&matrix);

		for axis in axes {
			assert!((length(axis) - 1.0).abs() < 1e-5);
		}

		assert!(dot(axes[0], axes[1]).abs() < 1e-5);
		assert!(dot(cross(axes[0], axes[1]), axes[2]) > 0.99);
	}

	#[test]
	fn normalizing_the_rows_removes_scale_and_the_last_column()
	{
		let mut matrix = Mat4([
			[2.0, 0.0, 0.0, 1.0],
			[0.0, 3.0, 0.0, 1.0],
			[0.0, 0.0, 4.0, 1.0],
			[5.0, 6.0, 7.0, 1.0],
		]);

		normalize_rotation_rows(&mut matrix);

		assert_eq!(matrix.0[0], [1.0, 0.0, 0.0, 0.0]);
		assert_eq!(matrix.0[2], [0.0, 0.0, 1.0, 0.0]);
		assert_eq!(matrix.0[3], [5.0, 6.0, 7.0, 1.0]);
	}

	use crate::backend::{BodySpec, RayHit, ShapeHandle};
	use raptor_anim::{RestPose, SkeletonData};

	#[derive(Default)]
	struct Recorder
	{
		parts: Vec<PartSpec>,
		activated: Vec<[f32; 3]>,
		destroyed: u32,
		poses: Vec<([f32; 3], [f32; 4])>,
		active: bool,
	}

	impl Backend for Recorder
	{
		fn make_box(&mut self, _h: [f32; 3], _d: f32, _c: f32) -> Result<ShapeHandle, String>
		{
			Err(String::new())
		}

		fn make_hull(&mut self, _p: &[[f32; 3]], _c: f32, _d: f32) -> Result<ShapeHandle, String>
		{
			Err(String::new())
		}

		fn make_mesh(&mut self, _p: &[[f32; 3]], _t: &[[u32; 3]]) -> Result<ShapeHandle, String>
		{
			Err(String::new())
		}

		fn release_shape(&mut self, _s: ShapeHandle) {}

		fn create_body(&mut self, _s: &BodySpec) -> Option<u32>
		{
			None
		}

		fn add_body(&mut self, _b: u32, _a: bool) {}

		fn remove_body(&mut self, _b: u32) {}

		fn destroy_body(&mut self, _b: u32) {}

		fn set_position_rotation(&mut self, _b: u32, _p: [f32; 3], _r: [f32; 4], _a: bool) {}

		fn position_rotation(&self, _b: u32) -> ([f32; 3], [f32; 4])
		{
			([0.0; 3], [0.0, 0.0, 0.0, 1.0])
		}

		fn cast_ray(&self, _o: [f32; 3], _d: [f32; 3], _i: Option<u32>) -> Option<RayHit>
		{
			None
		}

		fn cast_ray_all(&self, _o: [f32; 3], _d: [f32; 3]) -> Vec<(u32, f32)>
		{
			Vec::new()
		}

		fn surface_normal_along_ray(&self, _b: u32, _o: [f32; 3], _d: [f32; 3])
		-> Option<[f32; 3]>
		{
			None
		}

		fn step(&mut self, _t: f32, _s: u32) {}

		fn optimize_broad_phase(&mut self) {}

		fn is_dynamic(&self, _b: u32) -> bool
		{
			false
		}

		fn body_bounds(&self, _b: u32) -> ([f32; 3], [f32; 3])
		{
			([0.0; 3], [0.0; 3])
		}

		fn is_added(&self, _b: u32) -> bool
		{
			false
		}

		fn is_active(&self, _b: u32) -> bool
		{
			false
		}

		fn activate(&mut self, _b: u32) {}

		fn deactivate(&mut self, _b: u32) {}

		fn add_impulse(&mut self, _b: u32, _i: [f32; 3]) {}

		fn center_of_mass(&self, _b: u32) -> ([f32; 3], [f32; 4])
		{
			([0.0; 3], [0.0, 0.0, 0.0, 1.0])
		}

		fn angular_velocity(&self, _b: u32) -> [f32; 3]
		{
			[0.0; 3]
		}

		fn set_velocities(&mut self, _b: u32, _l: [f32; 3], _a: [f32; 3]) {}

		fn create_ragdoll(
			&mut self,
			parts: &[PartSpec],
			_serial: u32,
			_user_data: u64,
		) -> Option<u32>
		{
			self.parts = parts.to_vec();
			self.poses = parts
				.iter()
				.map(|part| (part.position, part.rotation))
				.collect();

			Some(7)
		}

		fn destroy_ragdoll(&mut self, _r: u32)
		{
			self.destroyed += 1;
		}

		fn ragdoll_activate(&mut self, _r: u32, velocity: [f32; 3])
		{
			self.activated.push(velocity);
			self.active = true;
		}

		fn ragdoll_is_active(&self, _r: u32) -> bool
		{
			self.active
		}

		fn ragdoll_body_pose(&self, _r: u32, index: u32) -> ([f32; 3], [f32; 4])
		{
			self.poses[index as usize]
		}
	}

	struct Messages(Vec<String>);

	impl Log for Messages
	{
		fn warn(&mut self, message: &str)
		{
			self.0.push(format!("warn: {message}"));
		}

		fn info(&mut self, message: &str)
		{
			self.0.push(format!("info: {message}"));
		}
	}

	fn humanoid(skip: Option<&str>) -> Skeleton
	{
		let joints: [(&str, &str); 19] = [
			("root", ""),
			("spine04", "root"),
			("spine03", "spine04"),
			("neck01", "spine03"),
			("head", "neck01"),
			("upperarm01.L", "spine03"),
			("lowerarm01.L", "upperarm01.L"),
			("wrist.L", "lowerarm01.L"),
			("upperarm01.R", "spine03"),
			("lowerarm01.R", "upperarm01.R"),
			("wrist.R", "lowerarm01.R"),
			("upperleg01.L", "root"),
			("lowerleg01.L", "upperleg01.L"),
			("foot.L", "lowerleg01.L"),
			("toe1-1.L", "foot.L"),
			("upperleg01.R", "root"),
			("lowerleg01.R", "upperleg01.R"),
			("foot.R", "lowerleg01.R"),
			("toe1-1.R", "foot.R"),
		];

		let joints: Vec<_> = joints
			.iter()
			.filter(|(name, _)| Some(*name) != skip)
			.collect();

		let index_of = |name: &str| joints.iter().position(|(joint, _)| *joint == name);

		let parents: Vec<u32> = joints
			.iter()
			.map(|(_, parent)| index_of(parent).map_or(NO_BONE, |index| index as u32))
			.collect();

		let names = joints.iter().map(|(name, _)| (*name).to_owned()).collect();

		let rest = RestPose {
			translation: [0.0, 0.3, 0.0, 0.0],
			..RestPose::default()
		};

		Skeleton::new(SkeletonData::new(
			joints.len() as u32,
			None,
			parents,
			Some(vec![rest; joints.len()]),
			None,
			names,
		))
	}

	fn options(log: &mut Messages) -> CreateOptions<'_>
	{
		CreateOptions {
			object_world: Mat4::IDENTITY,
			object_world_inverse: Mat4::IDENTITY,
			serial: 1,
			user_data: 5,
			log,
		}
	}

	#[test]
	fn a_humanoid_skeleton_gets_a_ragdoll_with_a_body_for_each_defined_bone()
	{
		let mut world = PhysicsWorld::new(Recorder::default());
		let mut skeleton = humanoid(None);
		let mut log = Messages(Vec::new());

		let ragdoll = Ragdoll::create(&mut world, &mut skeleton, options(&mut log)).unwrap();

		let parts = &world.backend().parts;

		assert_eq!(parts.len(), 13);
		assert_eq!(parts[0].parent, None);
		assert_eq!(parts[1].parent, Some(0));
		assert!(parts[1..].iter().all(|part| part.parent.is_some()));
		assert!(
			parts
				.iter()
				.all(|part| part.radius > 0.0 && part.half_height > 0.0)
		);
		assert_eq!(ragdoll.serial(), 1);
		assert!(
			log.0
				.iter()
				.any(|line| line.starts_with("info: Ragdoll: 13 bodies"))
		);
	}

	#[test]
	fn a_skeleton_that_is_not_a_humanoid_gets_no_ragdoll()
	{
		let mut world = PhysicsWorld::new(Recorder::default());

		let mut skeleton = Skeleton::new(SkeletonData::new(
			1,
			None,
			vec![NO_BONE],
			None,
			None,
			vec!["tail".to_owned()],
		));
		let mut log = Messages(Vec::new());

		assert!(Ragdoll::create(&mut world, &mut skeleton, options(&mut log)).is_none());
		assert!(
			log.0
				.iter()
				.any(|line| line.contains("does not look like a humanoid"))
		);
	}

	#[test]
	fn a_bone_missing_from_the_skeleton_is_skipped_with_a_warning()
	{
		let mut world = PhysicsWorld::new(Recorder::default());
		let mut skeleton = humanoid(Some("toe1-1.L"));
		let mut log = Messages(Vec::new());

		assert!(Ragdoll::create(&mut world, &mut skeleton, options(&mut log)).is_some());
		assert_eq!(world.backend().parts.len(), 12);
		assert!(
			log.0
				.iter()
				.any(|line| line.starts_with("warn:") && line.contains("toe1-1.L"))
		);
	}

	#[test]
	fn activating_hands_the_pose_to_the_ragdoll_and_destroying_takes_it_back()
	{
		let mut world = PhysicsWorld::new(Recorder::default());
		let mut skeleton = humanoid(None);
		let mut log = Messages(Vec::new());

		let ragdoll = Ragdoll::create(&mut world, &mut skeleton, options(&mut log)).unwrap();

		ragdoll.activate(&mut world, &mut skeleton, [0.0, 1.0, 0.0]);

		assert_eq!(world.backend().activated, vec![[0.0, 1.0, 0.0]]);
		assert_eq!(ragdoll.debug_boxes(&world).len(), 13);

		ragdoll.destroy(&mut world, Some(&mut skeleton));

		assert_eq!(world.backend().destroyed, 1);
	}
}
