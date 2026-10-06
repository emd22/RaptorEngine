use raptor_math::Vec3f;

pub const FACES: usize = 6;

const SAMPLE_COUNT: u32 = 128;
const MAX_HALF: f32 = 65504.0;

struct CubeLevel {
	size: u32,
	faces: [Vec<f32>; FACES],
}

struct LobeSample {
	direction: [f32; 3],
	weight: f32,
	lod: f32,
}

pub fn face_uv_to_direction(face: u32, u: f32, v: f32) -> Vec3f {
	match face {
		0 => Vec3f::new(1.0, -v, -u),
		1 => Vec3f::new(-1.0, -v, u),
		2 => Vec3f::new(u, 1.0, v),
		3 => Vec3f::new(u, -1.0, -v),
		4 => Vec3f::new(u, -v, 1.0),
		_ => Vec3f::new(-u, -v, -1.0),
	}
}

pub fn direction_to_face_uv(direction: Vec3f) -> (u32, f32, f32) {
	let (x, y, z) = (direction.x, direction.y, direction.z);
	let (ax, ay, az) = (x.abs(), y.abs(), z.abs());

	if ax >= ay && ax >= az {
		let inv = 1.0 / ax.max(1e-20);

		(
			if x > 0.0 { 0 } else { 1 },
			(if x > 0.0 { -z } else { z }) * inv,
			-y * inv,
		)
	} else if ay >= az {
		let inv = 1.0 / ay;

		(
			if y > 0.0 { 2 } else { 3 },
			x * inv,
			(if y > 0.0 { z } else { -z }) * inv,
		)
	} else {
		let inv = 1.0 / az;

		(
			if z > 0.0 { 4 } else { 5 },
			(if z > 0.0 { x } else { -x }) * inv,
			-y * inv,
		)
	}
}

pub fn texel_count(size: u32, mip_count: u32) -> u64 {
	(0..mip_count)
		.map(|mip| {
			let mip_size = u64::from((size >> mip).max(1));

			mip_size * mip_size * FACES as u64
		})
		.sum()
}

pub fn mip_to_roughness(mip: u32, mip_count: u32) -> f32 {
	if mip_count <= 1 {
		return 0.0;
	}

	mip as f32 / (mip_count - 1) as f32
}

pub fn float_to_half(value: f32) -> u16 {
	if value.is_nan() || value <= 0.0 {
		return 0;
	}

	let bits = value.min(MAX_HALF).to_bits();
	let exponent = ((bits >> 23) & 0xFF) as i32 - 127 + 15;
	let mantissa = bits & 0x7F_FFFF;

	if exponent <= 0 {
		if exponent < -10 {
			return 0;
		}

		let shift = (14 - exponent) as u32;
		let full = mantissa | 0x80_0000;

		return ((full >> shift) + ((full >> (shift - 1)) & 1)) as u16;
	}

	(((exponent as u32) << 10 | (mantissa >> 13)) + ((mantissa >> 12) & 1)) as u16
}

fn sample_face(level: &CubeLevel, face: usize, u: f32, v: f32) -> [f32; 3] {
	let size = level.size as f32;
	let max_coord = size - 1.0;

	let x = ((u * 0.5 + 0.5) * size - 0.5).clamp(0.0, max_coord);
	let y = ((v * 0.5 + 0.5) * size - 0.5).clamp(0.0, max_coord);

	let (x0, y0) = (x as u32, y as u32);
	let x1 = (x0 + 1).min(level.size - 1);
	let y1 = (y0 + 1).min(level.size - 1);

	let (fx, fy) = (x - x0 as f32, y - y0 as f32);

	let texels = &level.faces[face];
	let texel = |x: u32, y: u32| {
		let at = ((y * level.size + x) * 3) as usize;

		[texels[at], texels[at + 1], texels[at + 2]]
	};

	let (t00, t10, t01, t11) = (texel(x0, y0), texel(x1, y0), texel(x0, y1), texel(x1, y1));

	std::array::from_fn(|c| {
		let top = t00[c] + (t10[c] - t00[c]) * fx;
		let bottom = t01[c] + (t11[c] - t01[c]) * fx;

		top + (bottom - top) * fy
	})
}

fn sample_cube(levels: &[CubeLevel], direction: Vec3f, lod: f32) -> [f32; 3] {
	let (face, u, v) = direction_to_face_uv(direction);

	let max_lod = (levels.len() - 1) as f32;
	let lod = lod.clamp(0.0, max_lod);

	let lower = lod as usize;
	let upper = (lower + 1).min(levels.len() - 1);
	let blend = lod - lower as f32;

	let mut out = sample_face(&levels[lower], face as usize, u, v);

	if blend > 0.0 && upper != lower {
		let coarse = sample_face(&levels[upper], face as usize, u, v);

		for c in 0..3 {
			out[c] += (coarse[c] - out[c]) * blend;
		}
	}

	out
}

fn build_source_levels(faces: &[&[f32]; FACES], size: u32) -> Vec<CubeLevel> {
	let texels = size as usize * size as usize * 3;

	let mut levels = vec![CubeLevel {
		size,
		faces: std::array::from_fn(|face| faces[face][..texels].to_vec()),
	}];

	while levels.last().is_some_and(|level| level.size > 1) {
		let parent = levels.last().unwrap();
		let child_size = parent.size / 2;

		let child_faces = std::array::from_fn(|face| {
			let src = &parent.faces[face];
			let mut dst = Vec::with_capacity(child_size as usize * child_size as usize * 3);

			for y in 0..child_size {
				for x in 0..child_size {
					let row0 = (((y * 2) * parent.size + x * 2) * 3) as usize;
					let row1 = row0 + (parent.size * 3) as usize;

					for c in 0..3 {
						dst.push(
							(src[row0 + c] + src[row0 + c + 3] + src[row1 + c] + src[row1 + c + 3])
								* 0.25,
						);
					}
				}
			}

			dst
		});

		levels.push(CubeLevel {
			size: child_size,
			faces: child_faces,
		});
	}

	levels
}

fn lobe_samples(roughness: f32, source_size: u32) -> Vec<LobeSample> {
	let alpha = roughness * roughness;
	let alpha_sq = alpha * alpha;

	let texel_solid_angle =
		(4.0 * std::f32::consts::PI) / (6.0 * (source_size * source_size) as f32);

	let mut samples = Vec::with_capacity(SAMPLE_COUNT as usize);

	for i in 0..SAMPLE_COUNT {
		let xi_x = (i as f32 + 0.5) / SAMPLE_COUNT as f32;
		let xi_y = i.reverse_bits() as f32 * 2.328_306_4e-10;

		let phi = 2.0 * std::f32::consts::PI * xi_x;
		let cos_theta = ((1.0 - xi_y) / (1.0 + (alpha_sq - 1.0) * xi_y)).sqrt();
		let sin_theta = (1.0 - cos_theta * cos_theta).max(0.0).sqrt();

		let half_x = sin_theta * phi.cos();
		let half_y = sin_theta * phi.sin();
		let half_z = cos_theta;

		let n_dot_l = 2.0 * half_z * half_z - 1.0;

		if n_dot_l <= 0.0 {
			continue;
		}

		let denom = half_z * half_z * (alpha_sq - 1.0) + 1.0;
		let distribution = alpha_sq / (std::f32::consts::PI * denom * denom);

		let pdf = distribution * 0.25;
		let sample_solid_angle = 1.0 / (SAMPLE_COUNT as f32 * pdf);

		samples.push(LobeSample {
			direction: [2.0 * half_z * half_x, 2.0 * half_z * half_y, n_dot_l],
			weight: n_dot_l,
			lod: ((0.5 * (sample_solid_angle / texel_solid_angle).log2()) + 1.0).max(0.0),
		});
	}

	samples
}

fn write_texel(out: &mut Vec<u16>, rgb: [f32; 3]) {
	out.extend([
		float_to_half(rgb[0]),
		float_to_half(rgb[1]),
		float_to_half(rgb[2]),
		float_to_half(1.0),
	]);
}

pub fn prefilter(faces: &[&[f32]; FACES], size: u32, mip_count: u32) -> Vec<u16> {
	let levels = build_source_levels(faces, size);

	let mut out = Vec::with_capacity(texel_count(size, mip_count) as usize * 4);

	for face in &levels[0].faces {
		for texel in face.chunks_exact(3).take((size * size) as usize) {
			write_texel(&mut out, [texel[0], texel[1], texel[2]]);
		}
	}

	for mip in 1..mip_count {
		let mip_size = (size >> mip).max(1);
		let samples = lobe_samples(mip_to_roughness(mip, mip_count), size);

		for face in 0..FACES as u32 {
			for y in 0..mip_size {
				for x in 0..mip_size {
					let u = ((x as f32 + 0.5) / mip_size as f32) * 2.0 - 1.0;
					let v = ((y as f32 + 0.5) / mip_size as f32) * 2.0 - 1.0;

					let normal = face_uv_to_direction(face, u, v).normalize();
					let up = if normal.z.abs() < 0.999 {
						Vec3f::new(0.0, 0.0, 1.0)
					} else {
						Vec3f::new(1.0, 0.0, 0.0)
					};
					let tangent = up.cross(&normal).normalize();
					let bitangent = normal.cross(&tangent);

					let mut sum = [0.0f32; 3];
					let mut weight = 0.0;

					for sample in &samples {
						let direction = tangent * sample.direction[0]
							+ bitangent * sample.direction[1]
							+ normal * sample.direction[2];

						let radiance = sample_cube(&levels, direction, sample.lod);

						for c in 0..3 {
							sum[c] += radiance[c] * sample.weight;
						}

						weight += sample.weight;
					}

					let inv_weight = if weight > 0.0 { 1.0 / weight } else { 0.0 };

					write_texel(&mut out, sum.map(|value| value * inv_weight));
				}
			}
		}
	}

	out
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn face_uv_round_trips_through_a_direction() {
		for face in 0..6 {
			let direction = face_uv_to_direction(face, 0.25, -0.5).normalize();
			let (back, u, v) = direction_to_face_uv(direction);

			assert_eq!(back, face);
			assert!((u - 0.25).abs() < 1e-5 && (v + 0.5).abs() < 1e-5);
		}
	}

	#[test]
	fn halves_round_trip_the_common_values() {
		assert_eq!(float_to_half(0.0), 0);
		assert_eq!(float_to_half(1.0), 0x3C00);
		assert_eq!(float_to_half(-3.0), 0);
		assert_eq!(float_to_half(1.0e9), 0x7BFF);
	}

	#[test]
	fn a_uniform_sky_stays_uniform_at_every_roughness() {
		let size = 8;
		let face = vec![0.5f32; (size * size * 3) as usize];
		let faces = [face.as_slice(); FACES];

		let texels = prefilter(&faces, size, 3);

		assert_eq!(texels.len() as u64, texel_count(size, 3) * 4);

		for texel in texels.chunks_exact(4) {
			assert_eq!(texel[0], float_to_half(0.5));
		}
	}
}
