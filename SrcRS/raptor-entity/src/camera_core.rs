use raptor_math::{Mat4f, Vec3f};

pub const WEAPON_NEAR: f32 = 20.0;
pub const WEAPON_FAR: f32 = 0.01;
pub const PITCH_LIMIT_OFFSET: f32 = 0.01;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum ProjectionKind
{
	Perspective = 0,
	Orthographic = 1,
}

/// A camera's matrices, position and projection settings. The engine reads this straight out of
/// the record, so the layout is fixed: it is also described in the C header.
#[repr(C, align(16))]
#[derive(Clone)]
pub struct CameraCore
{
	pub view: [f32; 16],
	pub projection: [f32; 16],
	pub inv_view: [f32; 16],
	pub inv_projection: [f32; 16],
	pub camera_matrix: [f32; 16],
	pub weapon_camera_matrix: [f32; 16],
	pub weapon_projection: [f32; 16],
	pub position: [f32; 4],
	pub direction: [f32; 4],
	pub target: [f32; 4],
	pub angle_x: f32,
	pub angle_y: f32,
	pub z_near: f32,
	pub z_far: f32,
	pub fov_rad: f32,
	pub aspect: f32,
	pub weapon_fov: f32,
	pub width: f32,
	pub height: f32,
	pub kind: u32,
	pub update_transform: u32,
	pub update_projection: u32,
	pub look_at_target: u32,
}

fn vec3(values: [f32; 4]) -> Vec3f
{
	Vec3f::new(values[0], values[1], values[2])
}

fn pad(value: Vec3f) -> [f32; 4]
{
	let [x, y, z] = value.to_array();

	[x, y, z, 0.0]
}

impl CameraCore
{
	pub fn new(kind: ProjectionKind) -> Self
	{
		let identity = Mat4f::identity().to_rows();

		let mut core = Self {
			view: identity,
			projection: identity,
			inv_view: identity,
			inv_projection: identity,
			camera_matrix: identity,
			weapon_camera_matrix: identity,
			weapon_projection: identity,
			position: [0.0; 4],
			direction: [0.0, 0.0, 1.0, 0.0],
			target: [0.0; 4],
			angle_x: 0.0,
			angle_y: 0.0,
			z_near: 1000.0,
			z_far: 0.01,
			fov_rad: 80.0f32.to_radians(),
			aspect: 1.0,
			weapon_fov: 60.0f32.to_radians(),
			width: 45.0,
			height: 45.0,
			kind: kind as u32,
			update_transform: 1,
			update_projection: 1,
			look_at_target: 0,
		};

		core.update();

		core
	}

	fn is_orthographic(&self) -> bool
	{
		self.kind == ProjectionKind::Orthographic as u32
	}

	pub fn update_projection(&mut self)
	{
		let projection = if self.is_orthographic() {
			Mat4f::orthographic(self.width, self.height, self.z_near, self.z_far)
		} else {
			Mat4f::perspective(self.fov_rad, self.aspect, self.z_near, self.z_far)
		};

		self.projection = projection.to_rows();

		if !self.is_orthographic() {
			self.weapon_projection =
				Mat4f::perspective(self.weapon_fov, self.aspect, WEAPON_NEAR, WEAPON_FAR).to_rows();
		}

		self.update_projection = 0;
	}

	pub fn update_camera_matrix(&mut self)
	{
		self.update_projection();

		let view = Mat4f::from_rows(&self.view);
		let projection = Mat4f::from_rows(&self.projection);

		self.camera_matrix = (view * projection).to_rows();
		self.inv_view = view.inverse().to_rows();
		self.inv_projection = projection.inverse().to_rows();

		if !self.is_orthographic() {
			let weapon = Mat4f::from_rows(&self.weapon_projection);

			self.weapon_camera_matrix = (view * weapon).to_rows();
		}
	}

	pub fn update(&mut self)
	{
		let transform_changed = self.update_transform != 0;

		if !transform_changed && self.update_projection == 0 {
			return;
		}

		if transform_changed {
			self.direction = pad(look_direction(self.angle_x, self.angle_y));

			if self.look_at_target == 0 {
				self.target = pad(vec3(self.position) + vec3(self.direction));
			}

			self.view = Mat4f::look_at(vec3(self.position), vec3(self.target), Vec3f::UP).to_rows();
		}

		self.update_camera_matrix();

		self.update_transform = 0;
	}

	pub fn look_along(&mut self, position: Vec3f, direction: Vec3f, up: Vec3f)
	{
		self.view = Mat4f::look_at(position, position + direction, up).to_rows();
		self.update_camera_matrix();
	}

	pub fn rotate(&mut self, angle_x: f32, angle_y: f32)
	{
		self.angle_x = limit_rotation(self.angle_x + angle_x);

		let limit = std::f32::consts::FRAC_PI_2 - PITCH_LIMIT_OFFSET;

		self.angle_y = limit_rotation(self.angle_y + angle_y).clamp(-limit, limit);

		self.update_transform = 1;
	}

	pub fn move_by(&mut self, offset: Vec3f)
	{
		self.position = pad(vec3(self.position) + offset);
		self.update_transform = 1;
	}

	pub fn move_to(&mut self, position: Vec3f)
	{
		self.position = pad(position);
		self.update_transform = 1;
	}

	pub fn set_planes(&mut self, near: f32, far: f32)
	{
		self.z_near = near;
		self.z_far = far;
		self.update_projection = 1;
	}

	pub fn set_bounds(&mut self, width: f32, height: f32)
	{
		self.width = width;
		self.height = height;
		self.update_projection = 1;
	}
}

pub fn look_direction(angle_x: f32, angle_y: f32) -> Vec3f
{
	let (sin_x, cos_x) = angle_x.sin_cos();
	let (sin_y, cos_y) = angle_y.sin_cos();

	Vec3f::new(cos_y * sin_x, sin_y, cos_y * cos_x).normalize()
}

fn limit_rotation(value: f32) -> f32
{
	value % std::f32::consts::TAU
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn layout_is_fixed()
	{
		assert_eq!(std::mem::offset_of!(CameraCore, position), 448);
		assert_eq!(std::mem::offset_of!(CameraCore, angle_x), 496);
		assert_eq!(std::mem::offset_of!(CameraCore, kind), 532);
		assert_eq!(std::mem::size_of::<CameraCore>(), 560);
	}

	#[test]
	fn rotating_clamps_the_pitch_and_wraps_the_yaw()
	{
		let mut camera = CameraCore::new(ProjectionKind::Perspective);

		camera.rotate(7.0, 10.0);

		assert!(camera.angle_x.abs() < std::f32::consts::TAU);
		assert!(camera.angle_y < std::f32::consts::FRAC_PI_2);

		camera.update();

		assert_eq!(camera.update_transform, 0);
	}

	#[test]
	fn target_follows_direction_unless_looking_at_one()
	{
		let mut camera = CameraCore::new(ProjectionKind::Perspective);

		camera.move_to(Vec3f::new(1.0, 2.0, 3.0));
		camera.update();

		assert!((camera.target[2] - 4.0).abs() < 1e-6);

		camera.look_at_target = 1;
		camera.target = [9.0, 9.0, 9.0, 0.0];
		camera.move_by(Vec3f::new(1.0, 0.0, 0.0));
		camera.update();

		assert_eq!(camera.target[0], 9.0);
	}
}
