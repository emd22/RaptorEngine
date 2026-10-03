use raptor_mesh::Mesh;

use raptor_math::Vec3f;

use crate::{Brush, FaceTexture, Plane};

const TEXTURE_OFFSET_EPSILON: f32 = 1e-4;
const MIN_TEXTURE_SCALE: f32 = 1e-6;

struct Projection
{
	u_axis: Vec3f,
	v_axis: Vec3f,
}

fn projection(normal: Vec3f) -> Projection
{
	let abs = normal.abs();
	let (abs_x, abs_y, abs_z) = (abs.x, abs.y, abs.z);

	if abs_y >= abs_x && abs_y >= abs_z {
		Projection {
			u_axis: Vec3f::new(1.0, 0.0, 0.0),
			v_axis: Vec3f::new(0.0, 0.0, 1.0),
		}
	} else if abs_x >= abs_z {
		Projection {
			u_axis: Vec3f::new(0.0, 0.0, 1.0),
			v_axis: Vec3f::new(0.0, -1.0, 0.0),
		}
	} else {
		Projection {
			u_axis: Vec3f::new(1.0, 0.0, 0.0),
			v_axis: Vec3f::new(0.0, -1.0, 0.0),
		}
	}
}

pub fn default_texture_offset(normal: Vec3f, bounds_min: Vec3f, bounds_max: Vec3f) -> [f32; 2]
{
	let projection = projection(normal);
	let anchor = Vec3f::new(bounds_min.x, bounds_max.y, bounds_min.z);
	let scale = FaceTexture::default().scale;

	[
		-anchor.dot(&projection.u_axis) / scale[0],
		-anchor.dot(&projection.v_axis) / scale[1],
	]
}

pub fn world_aligned_texture(normal: Vec3f, origin: Vec3f) -> FaceTexture
{
	let projection = projection(normal);
	let mut texture = FaceTexture::default();

	texture.offset = [
		origin.dot(&projection.u_axis) / texture.scale[0],
		origin.dot(&projection.v_axis) / texture.scale[1],
	];

	texture
}

pub fn has_default_textures(planes: &[Plane], bounds_min: Vec3f, bounds_max: Vec3f) -> bool
{
	let default = FaceTexture::default();

	planes.iter().all(|plane| {
		let texture = &plane.texture;
		let default_offset = default_texture_offset(plane.normal, bounds_min, bounds_max);

		texture.scale[0] == default.scale[0]
			&& texture.scale[1] == default.scale[1]
			&& texture.rotation == 0.0
			&& (texture.offset[0] - default_offset[0]).abs() <= TEXTURE_OFFSET_EPSILON
			&& (texture.offset[1] - default_offset[1]).abs() <= TEXTURE_OFFSET_EPSILON
	})
}

fn safe_scale(scale: f32) -> f32
{
	if scale.abs() < MIN_TEXTURE_SCALE {
		if scale.is_sign_negative() {
			-MIN_TEXTURE_SCALE
		} else {
			MIN_TEXTURE_SCALE
		}
	} else {
		scale
	}
}

pub fn generate_mesh(brush: &Brush) -> Mesh
{
	let mut mesh = Mesh::default();

	if !brush.is_valid() {
		return mesh;
	}

	for face in &brush.faces {
		let plane = &brush.planes[face.plane_index as usize];
		let texture = &plane.texture;
		let normal = plane.normal;
		let projection = projection(normal);

		let scale = [safe_scale(texture.scale[0]), safe_scale(texture.scale[1])];

		let (mut sine, mut cosine) = (0.0f32, 1.0f32);
		if texture.rotation != 0.0 {
			let radians = texture.rotation * (std::f64::consts::PI / 180.0) as f32;
			sine = radians.sin();
			cosine = radians.cos();
		}

		let u_gradient = (projection.u_axis * cosine - projection.v_axis * sine) * (1.0 / scale[0]);
		let v_gradient = (projection.u_axis * sine + projection.v_axis * cosine) * (1.0 / scale[1]);

		let u_gradient = u_gradient - normal * normal.dot(&u_gradient);
		let v_gradient = v_gradient - normal * normal.dot(&v_gradient);

		let uu = u_gradient.dot(&u_gradient);
		let uv = u_gradient.dot(&v_gradient);
		let vv = v_gradient.dot(&v_gradient);

		let tangent = u_gradient * vv - v_gradient * uv;
		let tangent = (tangent * (1.0 / uu.mul_add(vv, -(uv * uv)))).normalize();

		let base = mesh.positions.len() as u32;

		for &vertex in &face.vertices {
			let u = vertex.dot(&projection.u_axis);
			let v = vertex.dot(&projection.v_axis);

			let new_u = u.mul_add(cosine, -(v * sine)) / scale[0] + texture.offset[0];
			let new_v = u.mul_add(sine, v * cosine) / scale[1] + texture.offset[1];

			mesh.positions.push(vertex.to_array());
			mesh.normals.push(normal.to_array());
			mesh.tangents.push(tangent.to_array());
			mesh.texcoords.push([new_u, new_v]);
		}

		for v in 1..face.vertices.len() as u32 - 1 {
			mesh.indices
				.extend_from_slice(&[base, base + v, base + v + 1]);
		}
	}

	mesh
}
