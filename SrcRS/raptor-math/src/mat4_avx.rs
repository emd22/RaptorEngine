/*
 * File:        mat4_avx.rs
 * Author:      emd22
 * Created:     04/10/2026
 * Description: SSE/AVX specific implementations for 4x4 matrices
 */

#[cfg(any(target_arch = "x86_64", feature = "simde"))]
pub mod mat4f_platform
{
	use crate::x86::*;

	use crate::vec3f_platform as v3;
	use crate::vec4f_platform as v4;

	/// The internal representation for a 4x4 matrix
	/// NOTE: Pass this by value wherever possible. Each value is encoded in the actual SIMD
	/// registers, so the allocation/copy cost is lower than passing by ref.
	pub type FLOAT44 = [__m128; 4];

	macro_rules! mul_row {
		($a: ident, $b: ident, $row_index: literal) => {{
			let a_row = $a[$row_index];
			let mut row = _mm_mul_ps(_mm_permute_ps::<0b00_00_00_00>(a_row), $b[0]);
			row = _mm_fmadd_ps(_mm_permute_ps::<0b01_01_01_01>(a_row), $b[1], row);
			row = _mm_fmadd_ps(_mm_permute_ps::<0b10_10_10_10>(a_row), $b[2], row);
			row = _mm_fmadd_ps(_mm_permute_ps::<0b11_11_11_11>(a_row), $b[3], row);
			row
		}};
	}

	#[inline(always)]
	pub fn zero() -> FLOAT44
	{
		unsafe { [_mm_setzero_ps(); 4] }
	}

	#[inline(always)]
	pub fn identity() -> FLOAT44
	{
		from_rows(&[
			1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
		])
	}

	#[inline(always)]
	pub fn from_rows(data: &[f32; 16]) -> FLOAT44
	{
		unsafe {
			[
				_mm_loadu_ps(data.as_ptr()),
				_mm_loadu_ps(data.as_ptr().add(4)),
				_mm_loadu_ps(data.as_ptr().add(8)),
				_mm_loadu_ps(data.as_ptr().add(12)),
			]
		}
	}

	#[inline(always)]
	pub fn from_columns(data: &[f32; 16]) -> FLOAT44
	{
		transpose(from_rows(data))
	}

	#[inline(always)]
	pub fn to_rows(m: FLOAT44) -> [f32; 16]
	{
		let mut data = [0.0f32; 16];

		unsafe {
			_mm_storeu_ps(data.as_mut_ptr(), m[0]);
			_mm_storeu_ps(data.as_mut_ptr().add(4), m[1]);
			_mm_storeu_ps(data.as_mut_ptr().add(8), m[2]);
			_mm_storeu_ps(data.as_mut_ptr().add(12), m[3]);
		}

		data
	}

	#[inline(always)]
	fn to_array(m: FLOAT44) -> [[f32; 4]; 4]
	{
		let flat = to_rows(m);

		[
			[flat[0], flat[1], flat[2], flat[3]],
			[flat[4], flat[5], flat[6], flat[7]],
			[flat[8], flat[9], flat[10], flat[11]],
			[flat[12], flat[13], flat[14], flat[15]],
		]
	}

	#[inline(always)]
	fn from_array(m: &[[f32; 4]; 4]) -> FLOAT44
	{
		unsafe {
			[
				_mm_loadu_ps(m[0].as_ptr()),
				_mm_loadu_ps(m[1].as_ptr()),
				_mm_loadu_ps(m[2].as_ptr()),
				_mm_loadu_ps(m[3].as_ptr()),
			]
		}
	}

	#[inline(always)]
	pub fn translation(position: [f32; 3]) -> FLOAT44
	{
		let mut m = identity();
		m[3] = v4::set(position[0], position[1], position[2], 1.0);
		m
	}

	#[inline(always)]
	pub fn scale(factors: [f32; 3]) -> FLOAT44
	{
		let mut m = identity();

		m[0] = v4::muls(m[0], factors[0]);
		m[1] = v4::muls(m[1], factors[1]);
		m[2] = v4::muls(m[2], factors[2]);

		m
	}

	#[inline(always)]
	pub fn get_translation(m: FLOAT44) -> v3::FLOAT4
	{
		v3::set_w(m[3], 0.0)
	}

	#[inline(always)]
	pub fn without_translation(m: FLOAT44) -> FLOAT44
	{
		[m[0], m[1], m[2], v4::set(0.0, 0.0, 0.0, 1.0)]
	}

	/// Multiplies one matrix by another matrix. `a` is applied first when transforming row vectors.
	#[inline(always)]
	pub fn mul(a: FLOAT44, b: FLOAT44) -> FLOAT44
	{
		unsafe {
			[
				mul_row!(a, b, 0),
				mul_row!(a, b, 1),
				mul_row!(a, b, 2),
				mul_row!(a, b, 3),
			]
		}
	}

	/// Transforms a row vector, `v * a`
	#[inline(always)]
	pub fn mul_vec4(a: FLOAT44, v: v4::FLOAT4) -> v4::FLOAT4
	{
		unsafe {
			let mut result = _mm_mul_ps(_mm_permute_ps::<0b00_00_00_00>(v), a[0]);
			result = _mm_fmadd_ps(_mm_permute_ps::<0b01_01_01_01>(v), a[1], result);
			result = _mm_fmadd_ps(_mm_permute_ps::<0b10_10_10_10>(v), a[2], result);
			result = _mm_fmadd_ps(_mm_permute_ps::<0b11_11_11_11>(v), a[3], result);

			result
		}
	}

	/// Transposes a matrix, rotates all row vectors to be column vectors and vice versa.
	#[inline(always)]
	pub fn transpose(m: FLOAT44) -> FLOAT44
	{
		unsafe {
			let lo01 = _mm_unpacklo_ps(m[0], m[1]);
			let lo23 = _mm_unpacklo_ps(m[2], m[3]);
			let hi01 = _mm_unpackhi_ps(m[0], m[1]);
			let hi23 = _mm_unpackhi_ps(m[2], m[3]);

			[
				_mm_movelh_ps(lo01, lo23),
				_mm_movehl_ps(lo23, lo01),
				_mm_movelh_ps(hi01, hi23),
				_mm_movehl_ps(hi23, hi01),
			]
		}
	}

	/// Transposes the upper 3x3 block. The fourth row of the result is left zero.
	#[inline(always)]
	pub fn transpose_mat3(m: FLOAT44) -> FLOAT44
	{
		unsafe { transpose([m[0], m[1], m[2], _mm_setzero_ps()]) }
	}

	/// The first twelve floats, as the C++ `CopyAsMat3To` copies them
	#[inline(always)]
	pub fn copy_as_mat3(m: FLOAT44) -> [f32; 12]
	{
		let flat = to_rows(m);

		let mut out = [0.0f32; 12];
		out.copy_from_slice(&flat[..12]);
		out
	}

	#[inline(always)]
	pub fn rotate_x(angle: f32) -> FLOAT44
	{
		let (s, c) = angle.sin_cos();

		[
			v4::set(1.0, 0.0, 0.0, 0.0),
			v4::set(0.0, c, s, 0.0),
			v4::set(0.0, -s, c, 0.0),
			v4::set(0.0, 0.0, 0.0, 1.0),
		]
	}

	#[inline(always)]
	pub fn rotate_y(angle: f32) -> FLOAT44
	{
		let (s, c) = angle.sin_cos();

		[
			v4::set(c, 0.0, -s, 0.0),
			v4::set(0.0, 1.0, 0.0, 0.0),
			v4::set(s, 0.0, c, 0.0),
			v4::set(0.0, 0.0, 0.0, 1.0),
		]
	}

	#[inline(always)]
	pub fn rotate_z(angle: f32) -> FLOAT44
	{
		let (s, c) = angle.sin_cos();

		[
			v4::set(c, s, 0.0, 0.0),
			v4::set(-s, c, 0.0, 0.0),
			v4::set(0.0, 0.0, 1.0, 0.0),
			v4::set(0.0, 0.0, 0.0, 1.0),
		]
	}

	#[inline(always)]
	pub fn rotation(x: f32, y: f32, z: f32, w: f32) -> FLOAT44
	{
		let tx = x * 2.0;
		let ty = y * 2.0;
		let tz = z * 2.0;

		let xx = tx * x;
		let yy = ty * y;
		let zz = tz * z;

		let xy = tx * y;
		let xz = tx * z;
		let xw = tx * w;

		let yz = ty * z;
		let yw = ty * w;
		let zw = tz * w;

		[
			v4::set((1.0 - yy) - zz, xy + zw, xz - yw, 0.0),
			v4::set(xy - zw, (1.0 - zz) - xx, yz + xw, 0.0),
			v4::set(xz + yw, yz - xw, (1.0 - xx) - yy, 0.0),
			v4::set(0.0, 0.0, 0.0, 1.0),
		]
	}

	/// The general inverse by cofactors. A singular matrix gives non-finite values, as in the C++
	/// version.
	pub fn inverse(m: FLOAT44) -> FLOAT44
	{
		let a = to_array(m);

		let s = [
			a[0][0] * a[1][1] - a[1][0] * a[0][1],
			a[0][0] * a[1][2] - a[1][0] * a[0][2],
			a[0][0] * a[1][3] - a[1][0] * a[0][3],
			a[0][1] * a[1][2] - a[1][1] * a[0][2],
			a[0][1] * a[1][3] - a[1][1] * a[0][3],
			a[0][2] * a[1][3] - a[1][2] * a[0][3],
		];

		let c = [
			a[2][0] * a[3][1] - a[3][0] * a[2][1],
			a[2][0] * a[3][2] - a[3][0] * a[2][2],
			a[2][0] * a[3][3] - a[3][0] * a[2][3],
			a[2][1] * a[3][2] - a[3][1] * a[2][2],
			a[2][1] * a[3][3] - a[3][1] * a[2][3],
			a[2][2] * a[3][3] - a[3][2] * a[2][3],
		];

		let idet = 1.0
			/ (s[0] * c[5] - s[1] * c[4] + s[2] * c[3] + s[3] * c[2] - s[4] * c[1] + s[5] * c[0]);

		let t = [
			[
				(a[1][1] * c[5] - a[1][2] * c[4] + a[1][3] * c[3]) * idet,
				(-a[0][1] * c[5] + a[0][2] * c[4] - a[0][3] * c[3]) * idet,
				(a[3][1] * s[5] - a[3][2] * s[4] + a[3][3] * s[3]) * idet,
				(-a[2][1] * s[5] + a[2][2] * s[4] - a[2][3] * s[3]) * idet,
			],
			[
				(-a[1][0] * c[5] + a[1][2] * c[2] - a[1][3] * c[1]) * idet,
				(a[0][0] * c[5] - a[0][2] * c[2] + a[0][3] * c[1]) * idet,
				(-a[3][0] * s[5] + a[3][2] * s[2] - a[3][3] * s[1]) * idet,
				(a[2][0] * s[5] - a[2][2] * s[2] + a[2][3] * s[1]) * idet,
			],
			[
				(a[1][0] * c[4] - a[1][1] * c[2] + a[1][3] * c[0]) * idet,
				(-a[0][0] * c[4] + a[0][1] * c[2] - a[0][3] * c[0]) * idet,
				(a[3][0] * s[4] - a[3][1] * s[2] + a[3][3] * s[0]) * idet,
				(-a[2][0] * s[4] + a[2][1] * s[2] - a[2][3] * s[0]) * idet,
			],
			[
				(-a[1][0] * c[3] + a[1][1] * c[1] - a[1][2] * c[0]) * idet,
				(a[0][0] * c[3] - a[0][1] * c[1] + a[0][2] * c[0]) * idet,
				(-a[3][0] * s[3] + a[3][1] * s[1] - a[3][2] * s[0]) * idet,
				(a[2][0] * s[3] - a[2][1] * s[1] + a[2][2] * s[0]) * idet,
			],
		];

		from_array(&t)
	}

	pub fn look_at(eye: v3::FLOAT4, target: v3::FLOAT4, up_vec: v3::FLOAT4) -> FLOAT44
	{
		let forward = v3::normalize(v3::sub(target, eye));
		let right = v3::normalize(v3::cross(up_vec, forward));
		let up = v3::cross(forward, right);

		let r = v3::get_values(right);
		let u = v3::get_values(up);
		let f = v3::get_values(forward);

		[
			v4::set(r[0], u[0], f[0], 0.0),
			v4::set(r[1], u[1], f[1], 0.0),
			v4::set(r[2], u[2], f[2], 0.0),
			v4::set(
				-v3::dot(eye, right),
				-v3::dot(eye, up),
				-v3::dot(eye, forward),
				1.0,
			),
		]
	}

	/// A reverse-Z perspective projection for a viewport with minDepth 1 and maxDepth 0. The plane
	/// arguments read backwards: the distance passed as `far_plane` lands at window depth 1 and the
	/// one passed as `near_plane` at 0, so callers pass the far distance first, as in the C++.
	pub fn perspective(yfov: f32, aspect_ratio: f32, near_plane: f32, far_plane: f32) -> FLOAT44
	{
		let height = 1.0 / (yfov * 0.5).tan();
		let width = height / aspect_ratio;

		let depth_range = near_plane / (far_plane - near_plane);

		[
			v4::set(width, 0.0, 0.0, 0.0),
			v4::set(0.0, -height, 0.0, 0.0),
			v4::set(0.0, 0.0, -depth_range, 1.0),
			v4::set(0.0, 0.0, far_plane * depth_range, 0.0),
		]
	}

	pub fn orthographic(width: f32, height: f32, near_plane: f32, far_plane: f32) -> FLOAT44
	{
		debug_assert!(near_plane > 0.0 && far_plane > 0.0);

		let depth_range = 1.0 / (far_plane - near_plane);

		[
			v4::set(2.0 / width, 0.0, 0.0, 0.0),
			v4::set(0.0, -2.0 / height, 0.0, 0.0),
			v4::set(0.0, 0.0, depth_range, 0.0),
			v4::set(0.0, 0.0, -depth_range * near_plane, 1.0),
		]
	}
}
