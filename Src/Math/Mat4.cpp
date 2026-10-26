#include <Math/Mat4.hpp>
#include <cstring>

namespace fx {

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

Mat4f Mat4f::AffineInverse() const
{
	const Vec3f r0(Rows[0]);
	const Vec3f r1(Rows[1]);
	const Vec3f r2(Rows[2]);
	const Vec3f t(Rows[3]);

	const Vec3f c0 = r1.Cross(r2);
	const Vec3f c1 = r2.Cross(r0);
	const Vec3f c2 = r0.Cross(r1);

	const float32 idet = 1.0f / r0.Dot(c0);

	Mat4f result;
	result.Rows[0].Set(c0.X * idet, c1.X * idet, c2.X * idet, 0.0f);
	result.Rows[1].Set(c0.Y * idet, c1.Y * idet, c2.Y * idet, 0.0f);
	result.Rows[2].Set(c0.Z * idet, c1.Z * idet, c2.Z * idet, 0.0f);
	result.Rows[3].Set(-t.Dot(c0) * idet, -t.Dot(c1) * idet, -t.Dot(c2) * idet, 1.0f);

	return result;
}

Mat4f Mat4f::RigidInverse() const
{
	const Vec3f r0(Rows[0]);
	const Vec3f r1(Rows[1]);
	const Vec3f r2(Rows[2]);
	const Vec3f t(Rows[3]);

	Mat4f result;
	result.Rows[0].Set(r0.X, r1.X, r2.X, 0.0f);
	result.Rows[1].Set(r0.Y, r1.Y, r2.Y, 0.0f);
	result.Rows[2].Set(r0.Z, r1.Z, r2.Z, 0.0f);
	result.Rows[3].Set(-t.Dot(r0), -t.Dot(r1), -t.Dot(r2), 1.0f);

	return result;
}

} // namespace fx
