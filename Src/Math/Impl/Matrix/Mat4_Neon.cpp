#include <Core/Defines.hpp>

#ifdef FX_USE_NEON
#include <arm_neon.h>

#include <Core/Assert.hpp>
#include <Math/Mat4.hpp>
#include <Math/Quat.hpp>
#include <cstring>

namespace fx {

const Mat4f Mat4f::scIdentity = Mat4f((float32[16]) { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 });

float32x4_t Mat4f::MultiplyVec4f_Neon(Vec4f& vec)
{
	float32x4_t result = vmovq_n_f32(0);

	result = vfmaq_laneq_f32(result, Rows[0].mIntrin, vec.mIntrin, 0);
	result = vfmaq_laneq_f32(result, Rows[1].mIntrin, vec.mIntrin, 1);
	result = vfmaq_laneq_f32(result, Rows[2].mIntrin, vec.mIntrin, 2);
	result = vfmaq_laneq_f32(result, Rows[3].mIntrin, vec.mIntrin, 3);

	return result;
}

Vec4f Mat4f::MultiplyVec4f(Vec4f& vec) { return Vec4f(MultiplyVec4f_Neon(vec)); }

Mat4f Mat4f::AsRotation(const Quat& quat)
{
	float x = quat.GetX();
	float y = quat.GetY();
	float z = quat.GetZ();
	float w = quat.GetW();

	const float tx = x * 2.0f;
	const float ty = y * 2.0f;
	const float tz = z * 2.0f;

	float xx = tx * x;
	float yy = ty * y;
	float zz = tz * z;

	float xy = tx * y;
	float xz = tx * z;
	float xw = tx * w;

	float yz = ty * z;
	float yw = ty * w;
	float zw = tz * w;

	return Mat4f(Vec4f((1.0f - yy) - zz, xy + zw, xz - yw, 0.0f), Vec4f(xy - zw, (1.0f - zz) - xx, yz + xw, 0.0f),
				 Vec4f(xz + yw, yz - xw, (1.0f - xx) - yy, 0.0f), Vec4f(0.0f, 0.0f, 0.0f, 1.0f));
}


Mat4f Mat4f::AsRotationX(float rad)
{
	Mat4f result;

	const float cr = cos(rad);
	const float sr = sin(rad);

	result.Rows[0].Set(1.0f, 0.0f, 0.0f, 0.0f);
	result.Rows[1].Set(0.0f, cr, sr, 0.0f);
	result.Rows[2].Set(0.0f, -sr, cr, 0.0f);
	result.Rows[3].Set(0.0f, 0.0f, 0.0f, 1.0f);

	return result;
}

Mat4f Mat4f::AsRotationY(float rad)
{
	Mat4f result;

	const float cr = cos(rad);
	const float sr = sin(rad);

	result.Rows[0].Set(cr, 0.0f, -sr, 0.0f);
	result.Rows[1].Set(0.0f, 1.0f, 0.0f, 0.0f);
	result.Rows[2].Set(sr, 0.0f, cr, 0.0f);
	result.Rows[3].Set(0.0f, 0.0f, 0.0f, 1.0f);

	return result;
}


Mat4f Mat4f::AsRotationZ(float rad)
{
	Mat4f result;

	const float cr = cos(rad);
	const float sr = sin(rad);

	result.Rows[0].Set(cr, sr, 0.0f, 0.0f);
	result.Rows[1].Set(-sr, cr, 0.0f, 0.0f);
	result.Rows[2].Set(0.0f, 0.0f, 1.0f, 0.0f);
	result.Rows[3].Set(0.0f, 0.0f, 0.0f, 1.0f);

	return result;
}

Mat4f Mat4f::operator*(const Mat4f& other) const
{
	Mat4f result;

	for (int i = 0; i < 4; i++) {
		float32x4_t r = vmulq_laneq_f32(other.Rows[0].mIntrin, Rows[i].mIntrin, 0);
		r = vfmaq_laneq_f32(r, other.Rows[1].mIntrin, Rows[i].mIntrin, 1);
		r = vfmaq_laneq_f32(r, other.Rows[2].mIntrin, Rows[i].mIntrin, 2);
		r = vfmaq_laneq_f32(r, other.Rows[3].mIntrin, Rows[i].mIntrin, 3);
		result.Rows[i].mIntrin = r;
	}

	return result;
}


Mat4f::Mat4f(float data[4][4]) noexcept
{
	Rows[0].mIntrin = vld1q_f32(data[0]);
	Rows[1].mIntrin = vld1q_f32(data[1]);
	Rows[2].mIntrin = vld1q_f32(data[2]);
	Rows[3].mIntrin = vld1q_f32(data[3]);
}

Mat4f Mat4f::Inverse() const
{
	float M[4][4];
	for (int i = 0; i < 4; i++) {
		memcpy(&M[i], Rows[i].mData, sizeof(float) * 4);
	}

	float T[4][4];

	float s[6];
	float c[6];
	s[0] = M[0][0] * M[1][1] - M[1][0] * M[0][1];
	s[1] = M[0][0] * M[1][2] - M[1][0] * M[0][2];
	s[2] = M[0][0] * M[1][3] - M[1][0] * M[0][3];
	s[3] = M[0][1] * M[1][2] - M[1][1] * M[0][2];
	s[4] = M[0][1] * M[1][3] - M[1][1] * M[0][3];
	s[5] = M[0][2] * M[1][3] - M[1][2] * M[0][3];

	c[0] = M[2][0] * M[3][1] - M[3][0] * M[2][1];
	c[1] = M[2][0] * M[3][2] - M[3][0] * M[2][2];
	c[2] = M[2][0] * M[3][3] - M[3][0] * M[2][3];
	c[3] = M[2][1] * M[3][2] - M[3][1] * M[2][2];
	c[4] = M[2][1] * M[3][3] - M[3][1] * M[2][3];
	c[5] = M[2][2] * M[3][3] - M[3][2] * M[2][3];

	float idet = 1.0f / (s[0] * c[5] - s[1] * c[4] + s[2] * c[3] + s[3] * c[2] - s[4] * c[1] + s[5] * c[0]);

	T[0][0] = (M[1][1] * c[5] - M[1][2] * c[4] + M[1][3] * c[3]) * idet;
	T[0][1] = (-M[0][1] * c[5] + M[0][2] * c[4] - M[0][3] * c[3]) * idet;
	T[0][2] = (M[3][1] * s[5] - M[3][2] * s[4] + M[3][3] * s[3]) * idet;
	T[0][3] = (-M[2][1] * s[5] + M[2][2] * s[4] - M[2][3] * s[3]) * idet;

	T[1][0] = (-M[1][0] * c[5] + M[1][2] * c[2] - M[1][3] * c[1]) * idet;
	T[1][1] = (M[0][0] * c[5] - M[0][2] * c[2] + M[0][3] * c[1]) * idet;
	T[1][2] = (-M[3][0] * s[5] + M[3][2] * s[2] - M[3][3] * s[1]) * idet;
	T[1][3] = (M[2][0] * s[5] - M[2][2] * s[2] + M[2][3] * s[1]) * idet;

	T[2][0] = (M[1][0] * c[4] - M[1][1] * c[2] + M[1][3] * c[0]) * idet;
	T[2][1] = (-M[0][0] * c[4] + M[0][1] * c[2] - M[0][3] * c[0]) * idet;
	T[2][2] = (M[3][0] * s[4] - M[3][1] * s[2] + M[3][3] * s[0]) * idet;
	T[2][3] = (-M[2][0] * s[4] + M[2][1] * s[2] - M[2][3] * s[0]) * idet;

	T[3][0] = (-M[1][0] * c[3] + M[1][1] * c[1] - M[1][2] * c[0]) * idet;
	T[3][1] = (M[0][0] * c[3] - M[0][1] * c[1] + M[0][2] * c[0]) * idet;
	T[3][2] = (-M[3][0] * s[3] + M[3][1] * s[1] - M[3][2] * s[0]) * idet;
	T[3][3] = (M[2][0] * s[3] - M[2][1] * s[1] + M[2][2] * s[0]) * idet;

	return Mat4f(T);
}

Mat4f Mat4f::GetWithoutTranslation() const
{
	Mat4f mat = *this;
	mat.Rows[3].Set(0.0f, 0.0f, 0.0f, 1.0f);

	return mat;
}

Mat4f Mat4f::Transposed() const
{
	float32x4x2_t tmp1 = vzipq_f32(Rows[0].mIntrin, Rows[2].mIntrin);
	float32x4x2_t tmp2 = vzipq_f32(Rows[1].mIntrin, Rows[3].mIntrin);
	float32x4x2_t tmp3 = vzipq_f32(tmp1.val[0], tmp2.val[0]);
	float32x4x2_t tmp4 = vzipq_f32(tmp1.val[1], tmp2.val[1]);

	Mat4f result;
	result.Rows[0].mIntrin = tmp3.val[0];
	result.Rows[1].mIntrin = tmp3.val[1];
	result.Rows[2].mIntrin = tmp4.val[0];
	result.Rows[3].mIntrin = tmp4.val[1];
	return result;
}

Mat4f Mat4f::TransposeMat3()
{
	float32x4x2_t tmp1 = vzipq_f32(Rows[0].mIntrin, Rows[2].mIntrin);
	float32x4x2_t tmp2 = vzipq_f32(Rows[1].mIntrin, vdupq_n_f32(0));
	float32x4x2_t tmp3 = vzipq_f32(tmp1.val[0], tmp2.val[0]);
	float32x4x2_t tmp4 = vzipq_f32(tmp1.val[1], tmp2.val[1]);

	Mat4f result;
	result.Rows[0].mIntrin = tmp3.val[0];
	result.Rows[1].mIntrin = tmp3.val[1];
	result.Rows[2].mIntrin = tmp4.val[0];

	return result;
}

void Mat4f::CopyAsMat3To(float* dest) const { memcpy(dest, RawData, sizeof(float32) * 12); }

void Mat4f::LookAt(const Vec3f& eye, const Vec3f& target, const Vec3f& upvec)
{
	Vec3f forward = (target - eye);
	forward.NormalizeIP();

	Vec3f right = upvec.Cross(forward);
	right.NormalizeIP();

	const Vec3f up = forward.Cross(right);

	Rows[0].Set(right.X, up.X, forward.X, 0.0f);
	Rows[1].Set(right.Y, up.Y, forward.Y, 0.0f);
	Rows[2].Set(right.Z, up.Z, forward.Z, 0.0f);
	Rows[3].Set(-eye.Dot(right), -eye.Dot(up), -eye.Dot(forward), 1.0f);
}

void Mat4f::LoadPerspectiveMatrix(float32 yfov, float32 aspect_ratio, float32 near_plane, float32 far_plane)
{
	LoadIdentity();

	const float32 height = 1.0f / tan(yfov * 0.5f);
	const float32 width = height / aspect_ratio;

	const float32 depth_range = near_plane / (far_plane - near_plane);

	Rows[0].X = (width);
	Rows[1].Y = (-height);

	Rows[2].Z = (-depth_range);
	Rows[2].W = (1.0f);

	Rows[3].Z = far_plane * depth_range;
	Rows[3].W = (0);
}

void Mat4f::LoadOrthographicMatrix(float32 width, float32 height, float32 near_plane, float32 far_plane)
{
	Assert(near_plane > 0.0f && far_plane > 0.0f);

	const float32 depth_range = 1.0f / (far_plane - near_plane);

	Rows[0].Set(2.0f / width, 0.0f, 0.0f, 0.0f);
	Rows[1].Set(0.0f, -2.0f / height, 0.0f, 0.0f);
	Rows[2].Set(0.0f, 0.0f, depth_range, 0.0f);
	Rows[3].Set(0.0f, 0.0f, -depth_range * near_plane, 1.0f);
}

} // namespace fx


#endif
