use crate::Mesh;
use crate::vec::{Vec3, length, normalized, sub, surface_normal};

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct FaceOptions {
	pub scale: f32,
	pub uv_min: [f32; 2],
	pub uv_max: [f32; 2],
}

impl Default for FaceOptions {
	fn default() -> Self {
		Self {
			scale: 1.0,
			uv_min: [0.0, 0.0],
			uv_max: [0.5, 0.5],
		}
	}
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct CubeOptions {
	pub left: FaceOptions,
	pub right: FaceOptions,
	pub top: FaceOptions,
	pub bottom: FaceOptions,
	pub front: FaceOptions,
	pub back: FaceOptions,
	pub align_uvs: bool,
}

pub fn emit_quad(
	mesh: &mut Mesh,
	verts: [Vec3; 4],
	options: &FaceOptions,
	flip_u: bool,
	flip_v: bool,
) {
	let normal = surface_normal(verts[0], verts[1], verts[2]);
	let base = mesh.positions.len() as u32;

	let along_u = if flip_u {
		sub(verts[0], verts[1])
	} else {
		sub(verts[1], verts[0])
	};
	let tangent = normalized(along_u);

	for vert in verts {
		mesh.positions.push(vert);
		mesh.normals.push(normal);
		mesh.tangents.push(tangent);
	}

	let width = length(sub(verts[1], verts[0]));
	let height = length(sub(verts[3], verts[0]));

	let span = [
		options.uv_max[0] - options.uv_min[0],
		options.uv_max[1] - options.uv_min[1],
	];
	let uv_min = options.uv_min;
	let uv_max = [uv_min[0] + span[0] * width, uv_min[1] + span[1] * height];

	let (u_left, u_right) = if flip_u {
		(uv_max[0], uv_min[0])
	} else {
		(uv_min[0], uv_max[0])
	};
	let (v_min, v_max) = if flip_v {
		(uv_max[1], uv_min[1])
	} else {
		(uv_min[1], uv_max[1])
	};

	mesh.texcoords.push([u_left, v_min]);
	mesh.texcoords.push([u_right, v_min]);
	mesh.texcoords.push([u_right, v_max]);
	mesh.texcoords.push([u_left, v_max]);

	mesh.indices
		.extend_from_slice(&[base, base + 1, base + 3, base + 1, base + 2, base + 3]);
}

pub fn cube(options: &CubeOptions) -> Mesh {
	let mut mesh = Mesh::default();

	let (left, right) = (options.left.scale, options.right.scale);
	let (top, bottom) = (options.top.scale, options.bottom.scale);
	let (front, back) = (options.front.scale, options.back.scale);

	let f_tl = [-left, top, front];
	let f_tr = [right, top, front];
	let f_bl = [-left, -bottom, front];
	let f_br = [right, -bottom, front];
	let b_tl = [-left, top, -back];
	let b_tr = [right, top, -back];
	let b_bl = [-left, -bottom, -back];
	let b_br = [right, -bottom, -back];

	let aligned = options.align_uvs;

	emit_quad(
		&mut mesh,
		[f_tl, f_tr, f_br, f_bl],
		&options.front,
		false,
		false,
	);
	emit_quad(
		&mut mesh,
		[b_tr, b_tl, b_bl, b_br],
		&options.back,
		aligned,
		false,
	);
	emit_quad(
		&mut mesh,
		[b_tl, b_tr, f_tr, f_tl],
		&options.top,
		false,
		false,
	);
	emit_quad(
		&mut mesh,
		[f_bl, f_br, b_br, b_bl],
		&options.bottom,
		false,
		aligned,
	);
	emit_quad(
		&mut mesh,
		[f_tr, b_tr, b_br, f_br],
		&options.right,
		aligned,
		false,
	);
	emit_quad(
		&mut mesh,
		[b_tl, f_tl, f_bl, b_bl],
		&options.left,
		false,
		false,
	);

	mesh
}
