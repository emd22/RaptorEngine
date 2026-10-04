use std::f64::consts::PI;

const SAMPLE_COUNT: u32 = 1024;
const MIN_N_DOT_V: f64 = 1e-3;

fn radical_inverse(bits: u32) -> f64
{
	f64::from(bits.reverse_bits()) * 2.328_306_436_538_696_3e-10
}

fn smith_ggx_correlated(n_dot_l: f64, n_dot_v: f64, alpha: f64) -> f64
{
	let alpha_sq = alpha * alpha;

	let lambda_v = n_dot_l * (n_dot_v * n_dot_v * (1.0 - alpha_sq) + alpha_sq).sqrt();
	let lambda_l = n_dot_v * (n_dot_l * n_dot_l * (1.0 - alpha_sq) + alpha_sq).sqrt();

	0.5 / (lambda_v + lambda_l)
}

pub fn integrate(n_dot_v: f64, roughness: f64) -> (f64, f64)
{
	let alpha = roughness * roughness;
	let alpha_sq = alpha * alpha;

	let view_x = (1.0 - n_dot_v * n_dot_v).sqrt();
	let view_z = n_dot_v;

	let mut scale = 0.0;
	let mut bias = 0.0;

	for i in 0..SAMPLE_COUNT {
		let u1 = (f64::from(i) + 0.5) / f64::from(SAMPLE_COUNT);
		let u2 = radical_inverse(i);

		let phi = 2.0 * PI * u1;
		let cos_theta = ((1.0 - u2) / (1.0 + (alpha_sq - 1.0) * u2)).sqrt();
		let sin_theta = (1.0 - cos_theta * cos_theta).max(0.0).sqrt();

		let half_x = sin_theta * phi.cos();
		let half_z = cos_theta;

		let v_dot_h = view_x * half_x + view_z * half_z;
		let n_dot_l = 2.0 * v_dot_h * half_z - view_z;

		if n_dot_l <= 0.0 || v_dot_h <= 0.0 {
			continue;
		}

		let weight =
			4.0 * smith_ggx_correlated(n_dot_l, n_dot_v, alpha) * v_dot_h * n_dot_l / half_z;
		let fresnel = (1.0 - v_dot_h).powf(5.0);

		scale += (1.0 - fresnel) * weight;
		bias += fresnel * weight;
	}

	(
		scale / f64::from(SAMPLE_COUNT),
		bias / f64::from(SAMPLE_COUNT),
	)
}

fn encode_unorm16(value: f64) -> u16
{
	(value.clamp(0.0, 1.0) * 65535.0 + 0.5) as u16
}

pub fn generate_lut(size: u32, out: &mut [u16]) -> bool
{
	let size = size as usize;

	if size == 0 || out.len() != size * size * 2 {
		return false;
	}

	let last_texel = (size - 1).max(1) as f64;

	for (y, row) in out.chunks_exact_mut(size * 2).enumerate() {
		let roughness = y as f64 / last_texel;

		for (x, texel) in row.as_chunks_mut::<2>().0.iter_mut().enumerate() {
			let n_dot_v = (x as f64 / last_texel).max(MIN_N_DOT_V);
			let (scale, bias) = integrate(n_dot_v, roughness);

			texel[0] = encode_unorm16(scale);
			texel[1] = encode_unorm16(bias);
		}
	}

	true
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn lut(size: u32) -> Vec<u16>
	{
		let mut out = vec![0; (size * size * 2) as usize];
		assert!(generate_lut(size, &mut out));
		out
	}

	fn texel(lut: &[u16], size: u32, x: u32, y: u32) -> (f64, f64)
	{
		let i = ((y * size + x) * 2) as usize;
		(f64::from(lut[i]) / 65535.0, f64::from(lut[i + 1]) / 65535.0)
	}

	#[test]
	fn rejects_a_wrong_sized_buffer()
	{
		assert!(!generate_lut(8, &mut [0; 10]));
		assert!(!generate_lut(0, &mut []));
	}

	#[test]
	fn a_one_texel_lut_is_finite()
	{
		assert_eq!(lut(1).len(), 2);
	}

	#[test]
	fn smooth_surfaces_reflect_everything_head_on()
	{
		let size = 32;
		let lut = lut(size);
		let (scale, bias) = texel(&lut, size, size - 1, 0);

		assert!((scale + bias - 1.0).abs() < 0.02, "{scale} {bias}");
	}

	#[test]
	fn energy_never_exceeds_one_and_fades_with_roughness()
	{
		let size = 32;
		let lut = lut(size);

		for y in 0..size {
			for x in 0..size {
				let (scale, bias) = texel(&lut, size, x, y);
				assert!(scale + bias <= 1.02, "{x} {y}: {scale} {bias}");
			}
		}

		let (smooth_scale, _) = texel(&lut, size, size / 2, 0);
		let (rough_scale, _) = texel(&lut, size, size / 2, size - 1);
		assert!(rough_scale < smooth_scale);
	}

	#[test]
	fn bias_grows_at_grazing_angles()
	{
		let size = 32;
		let lut = lut(size);
		let (_, head_on) = texel(&lut, size, size - 1, size / 2);
		let (_, grazing) = texel(&lut, size, 2, size / 2);

		assert!(grazing > head_on);
	}
}
