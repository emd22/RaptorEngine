#pragma once

#include "FVec3.hpp"
#include "FVec4.hpp"

#if FX_USE_NEON
#include <arm_neon.h>
#elif FX_USE_AVX
#include "SSE.hpp"
#endif


namespace fx {

class FQuat;

/**
 * @brief A single-precision 4x4 matrix class using SIMD instructions.
 *
 * Row-major order with row-vector convention (v * M).
 */
class alignas(16) FMat4
{
public:
	static const FMat4 scIdentity;

public:
	FMat4() noexcept
	{
		Rows[0].Set(0);
		Rows[1].Set(0);
		Rows[2].Set(0);
		Rows[3].Set(0);
	}

	FMat4(const float data[16]) noexcept
	{
		Rows[0] = FVec4(data);
		Rows[1] = FVec4(data + 4);
		Rows[2] = FVec4(data + 8);
		Rows[3] = FVec4(data + 12);
	}

	FMat4(float data[4][4]) noexcept;


	static FMat4 FromRows(float data[16]) { return FMat4(data); }

	static FMat4 FromColumns(float data[16])
	{
		return FMat4(FVec4(data[0], data[4], data[8], data[12]), FVec4(data[1], data[5], data[9], data[13]),
					 FVec4(data[2], data[6], data[10], data[14]), FVec4(data[3], data[7], data[11], data[15]));
	}

	static FMat4 AsTranslation(const FVec3 position)
	{
		FMat4 result = FMat4::scIdentity;
		result.Rows[3].Set(position.X, position.Y, position.Z, 1.0f);
		return result;
	}

	static FMat4 AsScale(const FVec3 scale)
	{
		FMat4 result = FMat4::scIdentity;

		result.Rows[0] *= scale.X;
		result.Rows[1] *= scale.Y;
		result.Rows[2] *= scale.Z;

		return result;
	}

	static FMat4 AsRotationX(float rad);
	static FMat4 AsRotationY(float rad);
	static FMat4 AsRotationZ(float rad);

	static FMat4 AsRotation(const FQuat quat);

	void LookAt(const FVec3 position, const FVec3 target, const FVec3 up);

	FMat4(const float scalar) noexcept
	{
		Rows[0].Set(scalar);
		Rows[1].Set(scalar);
		Rows[2].Set(scalar);
		Rows[3].Set(scalar);
	}

	FMat4(const FVec4 r0, const FVec4 r1, const FVec4 r2, const FVec4 r3) noexcept
	{
		Rows[0] = r0;
		Rows[1] = r1;
		Rows[2] = r2;
		Rows[3] = r3;
	}

	FMat4(FVec4 rows[4]) noexcept
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
		Rows[0] = FVec4(data);
		Rows[1] = FVec4(data + 4);
		Rows[2] = FVec4(data + 8);
		Rows[3] = FVec4(data + 12);
	}

	FMat4& operator=(const FMat4& other)
	{
		Rows[0] = other.Rows[0];
		Rows[1] = other.Rows[1];
		Rows[2] = other.Rows[2];
		Rows[3] = other.Rows[3];
		return *this;
	}

	FVec3 GetTranslation() const { return FVec3(Rows[3]); }

	FMat4 Inverse() const;

	FMat4 AffineInverse() const;
	FMat4 RigidInverse() const;

	void TransposeTo(FMat4& other) const;
	FMat4 Transposed() const;
	FMat4 TransposeMat3();

	void LoadPerspectiveMatrix(float32 hfov, float32 aspect_ratio, float32 near_plane, float32 far_plane);
	void LoadOrthographicMatrix(float32 width, float32 height, float32 near_plane, float32 far_plane);

	void CopyAsMat3To(float* dest) const;

	FMat4 operator*(const FMat4& other) const;
	FVec4 operator*(const FVec4 other) const;

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

	FMat4 GetWithoutTranslation() const;

public:
public:
	union alignas(16)
	{
		FVec4 Rows[4];
		float32 RawData[16];
	};

	friend class FVec4;

private:
};

} // namespace fx
