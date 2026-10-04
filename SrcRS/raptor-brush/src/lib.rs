mod rebuild;
mod texture;

use raptor_math::Vec3f;

pub const MAX_PLANES: usize = 32;
pub const NO_PLANE: i32 = -1;

const AXIS_ALIGNED_EPSILON: f32 = 1e-6;
const FIND_PLANE_EPSILON: f32 = 1e-3;
const ZERO_NORMAL_TOLERANCE: f32 = 0.00001;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct FaceTexture
{
	pub offset: [f32; 2],
	pub scale: [f32; 2],
	pub rotation: f32,
}

impl Default for FaceTexture
{
	fn default() -> Self
	{
		Self {
			offset: [0.0, 0.0],
			scale: [2.0, 2.0],
			rotation: 0.0,
		}
	}
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Plane
{
	pub normal: Vec3f,
	pub distance: f32,
	pub texture: FaceTexture,
}

impl Plane
{
	pub fn new(normal: Vec3f, distance: f32) -> Self
	{
		Self {
			normal,
			distance,
			texture: FaceTexture::default(),
		}
	}
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Face
{
	pub plane_index: u32,
	pub vertices: Vec<Vec3f>,
}

#[derive(Clone, Debug, Default)]
pub struct Brush
{
	pub planes: Vec<Plane>,
	pub faces: Vec<Face>,
	pub vertices: Vec<Vec3f>,
	pub bounds_min: Vec3f,
	pub bounds_max: Vec3f,
}

impl Brush
{
	pub fn from_box(min: Vec3f, max: Vec3f) -> Brush
	{
		let mut brush = Brush {
			planes: vec![
				// Right
				Plane::new(Vec3f::new(1.0, 0.0, 0.0), max.x),
				// Left
				Plane::new(Vec3f::new(-1.0, 0.0, 0.0), -min.x),
				// Up
				Plane::new(Vec3f::new(0.0, 1.0, 0.0), max.y),
				// Down
				Plane::new(Vec3f::new(0.0, -1.0, 0.0), -min.y),
				// Forward
				Plane::new(Vec3f::new(0.0, 0.0, 1.0), max.z),
				// Backward
				Plane::new(Vec3f::new(0.0, 0.0, -1.0), -min.z),
			],

			..Brush::default()
		};

		brush.rebuild();

		for plane_index in 0..brush.planes.len() {
			brush.reset_face_texture(plane_index);
		}

		brush
	}

	pub fn from_planes(planes: &[Plane]) -> Brush
	{
		let mut brush = Brush {
			planes: planes.to_vec(),
			..Brush::default()
		};
		brush.rebuild();
		brush
	}

	pub fn is_valid(&self) -> bool
	{
		!self.faces.is_empty()
	}

	pub fn is_box(&self) -> bool
	{
		if self.planes.len() != 6 {
			return false;
		}

		let mut directions = 0u32;

		for plane in &self.planes {
			let normal_values = plane.normal.to_array();
			let mut axis: Option<usize> = None;

			for (c, &component) in normal_values.iter().enumerate() {
				if (component.abs() - 1.0).abs() <= AXIS_ALIGNED_EPSILON {
					axis = Some(c);
				} else if component.abs() > AXIS_ALIGNED_EPSILON {
					return false;
				}
			}

			let Some(axis) = axis else { return false };

			directions |= 1 << (axis * 2 + usize::from(normal_values[axis] < 0.0));
		}

		directions == 0x3F
	}

	pub fn center(&self) -> Vec3f
	{
		(self.bounds_min + self.bounds_max) * 0.5f32
	}

	pub fn contains_point(&self, point: Vec3f, tolerance: f32) -> bool
	{
		if !self.is_valid() {
			return false;
		}

		self.planes
			.iter()
			.all(|plane| plane.normal.dot(&point) - plane.distance <= tolerance)
	}

	pub fn find_plane(&self, normal: Vec3f) -> i32
	{
		if normal
			.to_array()
			.iter()
			.all(|c| c.abs() <= ZERO_NORMAL_TOLERANCE)
		{
			return NO_PLANE;
		}

		let direction = normal.normalize();

		let mut best_plane = NO_PLANE;
		let mut best_dot = 1.0 - FIND_PLANE_EPSILON;

		for (index, plane) in self.planes.iter().enumerate() {
			let dot = plane.normal.dot(&direction);

			if dot > best_dot {
				best_dot = dot;
				best_plane = index as i32;
			}
		}

		best_plane
	}

	pub fn support(&self, direction: Vec3f) -> f32
	{
		self.vertices
			.iter()
			.map(|&v| direction.dot(&v))
			.reduce(f32::max)
			.unwrap_or(0.0)
	}

	pub fn face_center(&self, plane_index: u32) -> Vec3f
	{
		let Some(face) = self.faces.iter().find(|f| f.plane_index == plane_index) else {
			return Vec3f::splat(0.0);
		};

		let mut center = Vec3f::splat(0.0);

		for vertex in &face.vertices {
			center = center + vertex;
		}

		center * (1.0 / face.vertices.len() as f32)
	}

	pub fn raycast(&self, origin: Vec3f, direction: Vec3f) -> Option<(f32, u32)>
	{
		let mut enter_distance = -f32::MAX;
		let mut exit_distance = f32::MAX;
		let mut enter_plane = NO_PLANE;

		for (index, plane) in self.planes.iter().enumerate() {
			let facing = plane.normal.dot(&direction);
			let inside_distance = plane.distance - plane.normal.dot(&origin);

			if facing.abs() < AXIS_ALIGNED_EPSILON {
				if inside_distance < 0.0 {
					return None;
				}

				continue;
			}

			let distance = inside_distance / facing;

			if facing < 0.0 {
				if distance > enter_distance {
					enter_distance = distance;
					enter_plane = index as i32;
				}
			} else {
				exit_distance = exit_distance.min(distance);
			}
		}

		if enter_plane == NO_PLANE || enter_distance < 0.0 || enter_distance > exit_distance {
			return None;
		}

		Some((enter_distance, enter_plane as u32))
	}

	pub fn split(
		&self,
		normal: Vec3f,
		distance: f32,
		origin: Vec3f,
	) -> Option<(Vec<Plane>, Vec<Plane>)>
	{
		if !self.is_valid() || self.planes.len() >= MAX_PLANES {
			return None;
		}

		let negated = normal * -1.0;

		let mut back_planes = self.planes.clone();
		back_planes.push(Plane {
			normal,
			distance,
			texture: texture::world_aligned_texture(normal, origin),
		});

		let mut front_planes = self.planes.clone();
		front_planes.push(Plane {
			normal: negated,
			distance: -distance,
			texture: texture::world_aligned_texture(negated, origin),
		});

		let back = Brush::from_planes(&back_planes);
		let front = Brush::from_planes(&front_planes);

		if !back.is_valid() || !front.is_valid() {
			return None;
		}

		Some((back.planes, front.planes))
	}

	pub fn reset_face_texture(&mut self, plane_index: usize)
	{
		let normal = self.planes[plane_index].normal;
		self.planes[plane_index].texture = FaceTexture {
			offset: texture::default_texture_offset(normal, self.bounds_min, self.bounds_max),
			..FaceTexture::default()
		};
	}

	pub fn align_textures_to_world(&mut self, origin: Vec3f)
	{
		for plane in &mut self.planes {
			plane.texture = texture::world_aligned_texture(plane.normal, origin);
		}
	}

	pub fn has_default_textures(&self) -> bool
	{
		texture::has_default_textures(&self.planes, self.bounds_min, self.bounds_max)
	}

	pub fn generate_mesh(&self) -> raptor_mesh::Mesh
	{
		texture::generate_mesh(self)
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn unit_box() -> Brush
	{
		Brush::from_box(Vec3f::new(-1.0, -2.0, -3.0), Vec3f::new(1.0, 2.0, 3.0))
	}

	#[test]
	fn box_rebuilds_with_expected_bounds()
	{
		let brush = unit_box();

		assert!(brush.is_valid());
		assert!(brush.is_box());
		assert_eq!(brush.vertices.len(), 8);
		assert_eq!(brush.faces.len(), 6);
		assert_eq!(brush.bounds_min.to_array(), [-1.0, -2.0, -3.0]);
		assert_eq!(brush.bounds_max.to_array(), [1.0, 2.0, 3.0]);
		assert_eq!(brush.center().to_array(), [0.0, 0.0, 0.0]);
	}

	#[test]
	fn raycast_hits_nearest_face()
	{
		let brush = unit_box();
		let hit = brush.raycast(Vec3f::new(0.0, 0.0, -10.0), Vec3f::new(0.0, 0.0, 1.0));

		let (distance, plane) = hit.unwrap();
		assert_eq!(distance, 7.0);
		assert_eq!(
			brush.planes[plane as usize].normal.to_array(),
			[0.0, 0.0, -1.0]
		);
	}

	#[test]
	fn split_and_mesh()
	{
		let brush = unit_box();
		let split = brush.split(Vec3f::new(1.0, 0.0, 0.0), 0.0, Vec3f::splat(0.0));
		assert!(split.is_some());

		let mesh = brush.generate_mesh();
		assert_eq!(mesh.positions.len(), 24);
		assert_eq!(mesh.indices.len(), 36);
	}

	#[test]
	fn abs_clears_sign()
	{
		assert_eq!(
			Vec3f::new(-1.0, 2.0, -3.0).abs().to_array(),
			[1.0, 2.0, 3.0]
		);
	}
}
