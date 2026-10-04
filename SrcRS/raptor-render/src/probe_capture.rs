pub const FACES: usize = 6;
pub const SH_COEFFS: usize = 9;
pub const DEPTH_SIZE: usize = 16;
pub const DEPTH_TEXELS_PER_FACE: usize = DEPTH_SIZE * DEPTH_SIZE;
pub const MOMENT_TEXELS: usize = FACES * DEPTH_TEXELS_PER_FACE;
pub const DEPTH_MAX_DISTANCE: f32 = 50.0;
pub const RADIANCE_CLAMP: f32 = 65000.0;

const SH_NORM_0: f32 = 0.282095;
const SH_NORM_1: f32 = 0.488603;
const SH_NORM_2: f32 = 1.092548;
const SH_NORM_20: f32 = 0.315392;
const SH_NORM_22: f32 = 0.546274;

const COSINE_LOBE: [f32; SH_COEFFS] = [
	1.0,
	2.0 / 3.0,
	2.0 / 3.0,
	2.0 / 3.0,
	0.25,
	0.25,
	0.25,
	0.25,
	0.25,
];

pub type Mat4 = [f32; 16];
pub type ShCoeffs = [[f32; 4]; SH_COEFFS];

fn transform(row_vector: [f32; 4], m: &Mat4) -> [f32; 4]
{
	std::array::from_fn(|j| {
		row_vector[0] * m[j]
			+ row_vector[1] * m[4 + j]
			+ row_vector[2] * m[8 + j]
			+ row_vector[3] * m[12 + j]
	})
}

pub fn eval_sh_basis(d: [f32; 3]) -> [f32; SH_COEFFS]
{
	let [x, y, z] = d;

	[
		SH_NORM_0,
		SH_NORM_1 * y,
		SH_NORM_1 * z,
		SH_NORM_1 * x,
		SH_NORM_2 * x * y,
		SH_NORM_2 * y * z,
		SH_NORM_20 * (3.0 * z * z - 1.0),
		SH_NORM_2 * x * z,
		SH_NORM_22 * (x * x - y * y),
	]
}

pub fn sky_gradient_sh(sky: [f32; 3], ground: [f32; 3]) -> ShCoeffs
{
	let mut sh = [[0.0; 4]; SH_COEFFS];

	for c in 0..3 {
		sh[0][c] = (sky[c] + ground[c]) * 0.5 / SH_NORM_0;
		sh[1][c] = (sky[c] - ground[c]) * 0.5 / SH_NORM_1;
	}

	sh
}

pub fn direction_to_moment_texel(d: [f32; 3]) -> u32
{
	let a = d.map(f32::abs);

	let (face, u, v) = if a[0] >= a[1] && a[0] >= a[2] {
		let positive = d[0] > 0.0;
		(
			if positive { 0 } else { 1 },
			(if positive { -d[2] } else { d[2] }) / a[0],
			-d[1] / a[0],
		)
	} else if a[1] >= a[2] {
		let positive = d[1] > 0.0;
		(
			if positive { 2 } else { 3 },
			d[0] / a[1],
			(if positive { d[2] } else { -d[2] }) / a[1],
		)
	} else {
		let positive = d[2] > 0.0;
		(
			if positive { 4 } else { 5 },
			(if positive { d[0] } else { -d[0] }) / a[2],
			-d[1] / a[2],
		)
	};

	let to_texel = |coord: f32| ((coord * 0.5 + 0.5) * DEPTH_SIZE as f32).max(0.0) as usize;
	let clamped = |coord: f32| to_texel(coord).min(DEPTH_SIZE - 1);

	(face * DEPTH_TEXELS_PER_FACE + clamped(v) * DEPTH_SIZE + clamped(u)) as u32
}

pub fn half_to_float(half: u16) -> f32
{
	let exponent = u32::from((half >> 10) & 0x1F);
	let mantissa = u32::from(half & 0x3FF);

	if exponent == 0 || (exponent == 31 && mantissa != 0) {
		return 0.0;
	}

	let mut bits = u32::from(half & 0x8000) << 16;
	bits |= if exponent == 31 {
		0x7F80_0000
	} else {
		((exponent + 112) << 23) | (mantissa << 13)
	};

	f32::from_bits(bits)
}

pub fn depth_to_distance(inv_projection: &Mat4, stored_depth: f32, ndc_x: f32, ndc_y: f32) -> f32
{
	if stored_depth <= 0.0 {
		return DEPTH_MAX_DISTANCE;
	}

	let view = transform([ndc_x, ndc_y, 1.0 - stored_depth, 1.0], inv_projection);
	let distance =
		(view[0] * view[0] + view[1] * view[1] + view[2] * view[2]).sqrt() / view[3].abs();

	if distance < DEPTH_MAX_DISTANCE {
		distance
	} else {
		DEPTH_MAX_DISTANCE
	}
}

#[derive(Clone, Copy)]
struct Texel
{
	weighted_basis: [f32; SH_COEFFS],
	solid_angle: f32,
	moment_texel: u32,
}

pub struct Projection
{
	pub sh: ShCoeffs,
	pub moments: [f32; MOMENT_TEXELS * 2],
}

pub struct CaptureTable
{
	size: usize,
	inv_projection: Mat4,
	texels: Vec<Texel>,
}

impl CaptureTable
{
	pub fn new(
		size: usize,
		inv_projection: Mat4,
		face_inv_view_projection: &[Mat4; FACES],
	) -> Option<Self>
	{
		if size == 0 {
			return None;
		}

		let texel_area = 4.0 / (size * size) as f32;
		let mut texels = Vec::with_capacity(FACES * size * size);

		for inv_view_projection in face_inv_view_projection {
			for y in 0..size {
				for x in 0..size {
					let ndc_x = texel_to_ndc(x, size);
					let ndc_y = texel_to_ndc(y, size);

					let point = transform([ndc_x, ndc_y, 0.5, 1.0], inv_view_projection);
					let direction = normalize([
						point[0] / point[3],
						point[1] / point[3],
						point[2] / point[3],
					]);

					let dist_sq = 1.0 + ndc_x * ndc_x + ndc_y * ndc_y;
					let solid_angle = texel_area / (dist_sq * dist_sq.sqrt());

					texels.push(Texel {
						weighted_basis: eval_sh_basis(direction).map(|basis| basis * solid_angle),
						solid_angle,
						moment_texel: direction_to_moment_texel(direction),
					});
				}
			}
		}

		Some(Self {
			size,
			inv_projection,
			texels,
		})
	}

	pub fn size(&self) -> usize
	{
		self.size
	}

	pub fn project(&self, colors: [&[u16]; FACES], depths: [&[f32]; FACES]) -> Option<Projection>
	{
		let pixels = self.size * self.size;

		if colors.iter().any(|face| face.len() < pixels * 4)
			|| depths.iter().any(|face| face.len() < pixels)
		{
			return None;
		}

		let mut sh = [[0.0f32; 3]; SH_COEFFS];
		let mut moment_weight = [0.0f32; MOMENT_TEXELS];
		let mut moment_sum = [0.0f32; MOMENT_TEXELS];
		let mut moment_sum_sq = [0.0f32; MOMENT_TEXELS];

		for face in 0..FACES {
			let face_texels = &self.texels[face * pixels..(face + 1) * pixels];

			for (pixel, texel) in face_texels.iter().enumerate() {
				let rgba = &colors[face][pixel * 4..pixel * 4 + 3];

				for (c, &half) in rgba.iter().enumerate() {
					let sample = half_to_float(half);
					let radiance = if sample.is_nan() {
						0.0
					} else {
						sample.clamp(0.0, RADIANCE_CLAMP)
					};

					for (coefficient, basis) in sh.iter_mut().zip(&texel.weighted_basis) {
						coefficient[c] += radiance * basis;
					}
				}

				let x = pixel % self.size;
				let y = pixel / self.size;
				let distance = depth_to_distance(
					&self.inv_projection,
					depths[face][pixel],
					texel_to_ndc(x, self.size),
					texel_to_ndc(y, self.size),
				);

				let moment = texel.moment_texel as usize;
				moment_weight[moment] += texel.solid_angle;
				moment_sum[moment] += distance * texel.solid_angle;
				moment_sum_sq[moment] += distance * distance * texel.solid_angle;
			}
		}

		let mut projection = Projection {
			sh: [[0.0; 4]; SH_COEFFS],
			moments: [0.0; MOMENT_TEXELS * 2],
		};

		for (k, out) in projection.sh.iter_mut().enumerate() {
			for (c, value) in out.iter_mut().take(3).enumerate() {
				*value = sh[k][c] * COSINE_LOBE[k];
			}
		}

		for t in 0..MOMENT_TEXELS {
			let mut mean = DEPTH_MAX_DISTANCE;
			let mut mean_sq = DEPTH_MAX_DISTANCE * DEPTH_MAX_DISTANCE;

			if moment_weight[t] > 1e-9 {
				mean = moment_sum[t] / moment_weight[t];
				mean_sq = (moment_sum_sq[t] / moment_weight[t]).max(mean * mean);
			}

			projection.moments[t * 2] = mean;
			projection.moments[t * 2 + 1] = mean_sq;
		}

		Some(projection)
	}
}

fn texel_to_ndc(index: usize, size: usize) -> f32
{
	((index as f32 + 0.5) / size as f32) * 2.0 - 1.0
}

fn normalize(v: [f32; 3]) -> [f32; 3]
{
	let length = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt();
	v.map(|component| component / length)
}

#[cfg(test)]
mod tests
{
	use super::*;

	const SIZE: usize = 16;

	fn face_matrix(right: [f32; 3], up: [f32; 3], forward: [f32; 3]) -> Mat4
	{
		[
			right[0],
			right[1],
			right[2],
			0.0,
			up[0],
			up[1],
			up[2],
			0.0,
			forward[0] * 2.0,
			forward[1] * 2.0,
			forward[2] * 2.0,
			0.0,
			0.0,
			0.0,
			0.0,
			1.0,
		]
	}

	fn faces() -> [Mat4; FACES]
	{
		[
			face_matrix([0.0, 0.0, -1.0], [0.0, 1.0, 0.0], [1.0, 0.0, 0.0]),
			face_matrix([0.0, 0.0, 1.0], [0.0, 1.0, 0.0], [-1.0, 0.0, 0.0]),
			face_matrix([1.0, 0.0, 0.0], [0.0, 0.0, 1.0], [0.0, 1.0, 0.0]),
			face_matrix([1.0, 0.0, 0.0], [0.0, 0.0, -1.0], [0.0, -1.0, 0.0]),
			face_matrix([1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]),
			face_matrix([-1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, -1.0]),
		]
	}

	fn identity() -> Mat4
	{
		let mut m = [0.0; 16];
		for i in 0..4 {
			m[i * 5] = 1.0;
		}
		m
	}

	fn half_one() -> u16
	{
		0x3C00
	}

	#[test]
	fn texel_solid_angles_cover_the_sphere()
	{
		let table = CaptureTable::new(SIZE, identity(), &faces()).unwrap();
		let total: f32 = table.texels.iter().map(|texel| texel.solid_angle).sum();

		assert!((total - 4.0 * std::f32::consts::PI).abs() < 0.05, "{total}");
	}

	#[test]
	fn every_moment_texel_is_reached()
	{
		let table = CaptureTable::new(64, identity(), &faces()).unwrap();
		let mut hit = [false; MOMENT_TEXELS];

		for texel in &table.texels {
			hit[texel.moment_texel as usize] = true;
		}

		assert!(hit.iter().all(|&h| h));
	}

	#[test]
	fn uniform_radiance_projects_to_a_flat_dc_term()
	{
		let table = CaptureTable::new(SIZE, identity(), &faces()).unwrap();
		let colors = vec![half_one(); SIZE * SIZE * 4];
		let depths = vec![0.0f32; SIZE * SIZE];

		let projection = table
			.project([&colors[..]; FACES], [&depths[..]; FACES])
			.unwrap();

		let expected = SH_NORM_0 * 4.0 * std::f32::consts::PI;
		assert!(
			(projection.sh[0][0] - expected).abs() < 0.01,
			"{}",
			projection.sh[0][0]
		);

		for k in 1..SH_COEFFS {
			assert!(
				projection.sh[k][1].abs() < 0.01,
				"{k}: {}",
				projection.sh[k][1]
			);
		}
	}

	#[test]
	fn missing_geometry_reads_as_the_far_distance()
	{
		let table = CaptureTable::new(SIZE, identity(), &faces()).unwrap();
		let colors = vec![0u16; SIZE * SIZE * 4];
		let depths = vec![0.0f32; SIZE * SIZE];

		let projection = table
			.project([&colors[..]; FACES], [&depths[..]; FACES])
			.unwrap();

		for t in 0..MOMENT_TEXELS {
			assert!((projection.moments[t * 2] - DEPTH_MAX_DISTANCE).abs() < 1e-3);
			assert!(
				(projection.moments[t * 2 + 1] - DEPTH_MAX_DISTANCE * DEPTH_MAX_DISTANCE).abs()
					< 0.1
			);
		}
	}

	#[test]
	fn bad_radiance_is_clamped_or_dropped()
	{
		let table = CaptureTable::new(SIZE, identity(), &faces()).unwrap();
		let depths = vec![0.0f32; SIZE * SIZE];

		let project = |half: u16| {
			let colors = vec![half; SIZE * SIZE * 4];
			table
				.project([&colors[..]; FACES], [&depths[..]; FACES])
				.unwrap()
				.sh[0][0]
		};

		let nan = project(0x7E00);
		let inf = project(0x7C00);
		let negative = project(0xBC00);

		assert_eq!(nan, 0.0);
		assert_eq!(negative, 0.0);
		assert!(inf.is_finite() && inf > 1000.0);
	}

	#[test]
	fn short_buffers_are_rejected()
	{
		let table = CaptureTable::new(SIZE, identity(), &faces()).unwrap();
		let colors = vec![0u16; SIZE * SIZE * 4 - 1];
		let full_colors = vec![0u16; SIZE * SIZE * 4];
		let depths = vec![0.0f32; SIZE * SIZE];

		let mut color_faces = [&full_colors[..]; FACES];
		color_faces[3] = &colors;

		assert!(table.project(color_faces, [&depths[..]; FACES]).is_none());
		assert!(CaptureTable::new(0, identity(), &faces()).is_none());
	}

	#[test]
	fn half_decoding_matches_ieee()
	{
		assert_eq!(half_to_float(0x3C00), 1.0);
		assert_eq!(half_to_float(0xC000), -2.0);
		assert_eq!(half_to_float(0x0001), 0.0);
		assert_eq!(half_to_float(0x7E00), 0.0);
		assert_eq!(half_to_float(0x7C00), f32::INFINITY);
		assert_eq!(half_to_float(0x7BFF), 65504.0);
	}

	#[test]
	fn axis_directions_pick_their_face_centre()
	{
		let centre = (DEPTH_SIZE / 2) * DEPTH_SIZE + DEPTH_SIZE / 2;
		let faces = [
			[1.0, 0.0, 0.0],
			[-1.0, 0.0, 0.0],
			[0.0, 1.0, 0.0],
			[0.0, -1.0, 0.0],
			[0.0, 0.0, 1.0],
			[0.0, 0.0, -1.0],
		];

		for (face, d) in faces.into_iter().enumerate() {
			assert_eq!(
				direction_to_moment_texel(d) as usize,
				face * DEPTH_TEXELS_PER_FACE + centre
			);
		}
	}

	#[test]
	fn sky_gradient_looks_up_and_down()
	{
		let sky = [1.0, 0.5, 0.25];
		let ground = [0.25, 0.25, 0.25];
		let sh = sky_gradient_sh(sky, ground);

		let eval = |d: [f32; 3], c: usize| -> f32 {
			eval_sh_basis(d)
				.iter()
				.zip(&sh)
				.map(|(basis, coefficient)| basis * coefficient[c])
				.sum()
		};

		for c in 0..3 {
			assert!((eval([0.0, 1.0, 0.0], c) - sky[c]).abs() < 1e-3);
			assert!((eval([0.0, -1.0, 0.0], c) - ground[c]).abs() < 1e-3);
		}
	}

	#[test]
	fn depth_conversion_clamps_to_the_far_distance()
	{
		let m = identity();

		assert_eq!(depth_to_distance(&m, 0.0, 0.0, 0.0), DEPTH_MAX_DISTANCE);
		assert_eq!(depth_to_distance(&m, 0.5, 0.0, 0.0), 0.5);
		assert_eq!(
			depth_to_distance(&m, f32::NAN, 0.0, 0.0),
			DEPTH_MAX_DISTANCE
		);
	}
}
