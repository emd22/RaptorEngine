#pragma once

#include "Vec3.hpp"
#include "Vec4.hpp"

#if FX_USE_NEON
#include <arm_neon.h>
#elif FX_USE_AVX
#include "SSE.hpp"
#endif


namespace fx {

class Quat;

/**
 * @brief A single-precision 4x4 matrix class using SIMD instructions.
 *
 * Row-major order with row-vector convention (v * M).
 */
class alignas(16) Mat4f
{
public:
	static const Mat4f scIdentity;

public:
	Mat4f() noexcept
	{
		Rows[0].Set(0);
		Rows[1].Set(0);
		Rows[2].Set(0);
		Rows[3].Set(0);
	}

	Mat4f(const float data[16]) noexcept
	{
		Rows[0] = Vec4f(data);
		Rows[1] = Vec4f(data + 4);
		Rows[2] = Vec4f(data + 8);
		Rows[3] = Vec4f(data + 12);
	}

	Mat4f(float data[4][4]) noexcept;


	static Mat4f FromRows(float data[16]) { return Mat4f(data); }

	static Mat4f FromColumns(float data[16])
	{
		return Mat4f(Vec4f(data[0], data[4], data[8], data[12]), Vec4f(data[1], data[5], data[9], data[13]),
					 Vec4f(data[2], data[6], data[10], data[14]), Vec4f(data[3], data[7], data[11], data[15]));
	}

	static Mat4f AsTranslation(const Vec3f& position)
	{
		Mat4f result = Mat4f::scIdentity;
		result.Rows[3].Set(position.X, position.Y, position.Z, 1.0f);
		return result;
	}

	static Mat4f AsScale(const Vec3f& scale)
	{
		Mat4f result = Mat4f::scIdentity;

		result.Rows[0] *= scale.X;
		result.Rows[1] *= scale.Y;
		result.Rows[2] *= scale.Z;

		return result;
	}

	static Mat4f AsRotationX(float rad);
	static Mat4f AsRotationY(float rad);
	static Mat4f AsRotationZ(float rad);

	static Mat4f AsRotation(const Quat& quat);

	void LookAt(const Vec3f& position, const Vec3f& target, const Vec3f& up);

	Mat4f(const float scalar) noexcept
	{
		Rows[0].Set(scalar);
		Rows[1].Set(scalar);
		Rows[2].Set(scalar);
		Rows[3].Set(scalar);
	}

	Mat4f(const Vec4f& r0, const Vec4f& r1, const Vec4f& r2, const Vec4f& r3) noexcept
	{
		Rows[0] = r0;
		Rows[1] = r1;
		Rows[2] = r2;
		Rows[3] = r3;
	}

	Mat4f(Vec4f rows[4]) noexcept
	{
		Rows[0] = rows[0];
		Rows[1] = rows[1];
		Rows[2] = rows[2];
		Rows[3] = rows[3];
	}

	inline void LoadIdentity()
	{
		Rows[0].Set(1.0f, 0.0f, 0.0f, 0.0f);
		Rows[1].Set(0.0f, 1.0f, 0.0f, 0.0f);
		Rows[2].Set(0.0f, 0.0f, 1.0f, 0.0f);
		Rows[3].Set(0.0f, 0.0f, 0.0f, 1.0f);
	}

	void Set(float data[16])
	{
		Rows[0] = Vec4f(data);
		Rows[1] = Vec4f(data + 4);
		Rows[2] = Vec4f(data + 8);
		Rows[3] = Vec4f(data + 12);
	}

	Mat4f& operator=(const Mat4f& other)
	{
		Rows[0] = other.Rows[0];
		Rows[1] = other.Rows[1];
		Rows[2] = other.Rows[2];
		Rows[3] = other.Rows[3];
		return *this;
	}

	Vec3f GetTranslation() const { return Vec3f(Rows[3]); }

	Mat4f Inverse() const;
	Mat4f Transposed() const;
	Mat4f TransposeMat3();

	void LoadPerspectiveMatrix(float32 hfov, float32 aspect_ratio, float32 near_plane, float32 far_plane);
	void LoadOrthographicMatrix(float32 width, float32 height, float32 near_plane, float32 far_plane);

	void CopyAsMat3To(float* dest) const;

	Mat4f operator*(const Mat4f& other) const;
	Vec4f operator*(const Vec4f& other) const;

	void Print() const
	{
		printf("\t=== Matrix ===\n");
		for (int i = 0; i < 4; i++) {
			printf("{ %.06f\t%.06f\t%.06f\t%.06f }", static_cast<double>(Rows[i].mData[0]),
				   static_cast<double>(Rows[i].mData[1]), static_cast<double>(Rows[i].mData[2]),
				   static_cast<double>(Rows[i].mData[3]));
			putchar('\n');
		}
	}

	Vec4f MultiplyVec4f(Vec4f& vec);

	Mat4f GetWithoutTranslation() const;

public:
#if defined(FX_USE_NEON)
	float32x4_t MultiplyVec4f_Neon(Vec4f& vec);
#elif defined(FX_USE_AVX)
	__m128 MultiplyVec4f_SSE(const Vec4f& vec);
#endif

public:
	union alignas(16)
	{
		Vec4f Rows[4];
		float32 RawData[16];
	};

	friend class Vec4f;

private:
};

} // namespace fx
