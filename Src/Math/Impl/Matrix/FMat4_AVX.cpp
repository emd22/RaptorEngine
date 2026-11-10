#include <Core/Defines.hpp>

#ifdef FX_USE_AVX

#include <Core/Assert.hpp>
#include <Core/Types.hpp>
#include <Math/FMat4.hpp>
#include <Math/FQuat.hpp>
#include <Math/SSE.hpp>
#include <Math/SSEUtil.hpp>
#include <cstring>


namespace fx {

static const float32 scIdentityData[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
											0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

const FMat4 FMat4::scIdentity = FMat4(scIdentityData);

FMat4 FMat4::AsRotation(const Quat quat)
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

	return FMat4(FVec4((1.0f - yy) - zz, xy + zw, xz - yw, 0.0f), FVec4(xy - zw, (1.0f - zz) - xx, yz + xw, 0.0f),
				 FVec4(xz + yw, yz - xw, (1.0f - xx) - yy, 0.0f), FVec4(0.0f, 0.0f, 0.0f, 1.0f));
}

void FMat4::LoadPerspectiveMatrix(float32 yfov, float32 aspect_ratio, float32 near_plane, float32 far_plane)
{
	LoadIdentity();

	const float32 height = 1.0f / tan(yfov * 0.5f);
	const float32 width = height / aspect_ratio;

	const float32 depth_range = near_plane / (far_plane - near_plane);

	Rows[0].X = (width);
	Rows[1].Y = (-height);

	Rows[2].Z = (-depth_range);
	Rows[2].W = (1.0f);

	Rows[3].Z = (far_plane * depth_range);
	Rows[3].W = (0);
}


void FMat4::LoadOrthographicMatrix(float32 width, float32 height, float32 near_plane, float32 far_plane)
{
	Assert(near_plane > 0.0f && far_plane > 0.0f);

	const float32 depth_range = 1.0f / (far_plane - near_plane);

	Rows[0].Set(2.0f / width, 0.0f, 0.0f, 0.0f);
	Rows[1].Set(0.0f, -2.0f / height, 0.0f, 0.0f);
	Rows[2].Set(0.0f, 0.0f, depth_range, 0.0f);
	Rows[3].Set(0.0f, 0.0f, -depth_range * near_plane, 1.0f);
}


FMat4 FMat4::AsRotationX(float rad)
{
	FMat4 result;

	const float cr = cos(rad);
	const float sr = sin(rad);

	result.Rows[0].Set(1.0f, 0.0f, 0.0f, 0.0f);
	result.Rows[1].Set(0.0f, cr, sr, 0.0f);
	result.Rows[2].Set(0.0f, -sr, cr, 0.0f);
	result.Rows[3].Set(0.0f, 0.0f, 0.0f, 1.0f);

	return result;
}

FMat4 FMat4::AsRotationY(float rad)
{
	FMat4 result;

	const float cr = cos(rad);
	const float sr = sin(rad);

	result.Rows[0].Set(cr, 0.0f, -sr, 0.0f);
	result.Rows[1].Set(0.0f, 1.0f, 0.0f, 0.0f);
	result.Rows[2].Set(sr, 0.0f, cr, 0.0f);
	result.Rows[3].Set(0.0f, 0.0f, 0.0f, 1.0f);

	return result;
}


FMat4 FMat4::AsRotationZ(float rad)
{
	FMat4 result;

	const float cr = cos(rad);
	const float sr = sin(rad);

	result.Rows[0].Set(cr, sr, 0.0f, 0.0f);
	result.Rows[1].Set(-sr, cr, 0.0f, 0.0f);
	result.Rows[2].Set(0.0f, 0.0f, 1.0f, 0.0f);
	result.Rows[3].Set(0.0f, 0.0f, 0.0f, 1.0f);

	return result;
}


FMat4 FMat4::operator*(const FMat4& other) const
{
	FMat4 result;

	for (int i = 0; i < 4; i++) {
		const __m128 a = Rows[i].mIntrin;
		__m128 r = _mm_mul_ps(simd::Permute4<simd::Shuffle_AX>(a), other.Rows[0].mIntrin);
		r = _mm_fmadd_ps(simd::Permute4<simd::Shuffle_AY>(a), other.Rows[1].mIntrin, r);
		r = _mm_fmadd_ps(simd::Permute4<simd::Shuffle_AZ>(a), other.Rows[2].mIntrin, r);
		r = _mm_fmadd_ps(simd::Permute4<simd::Shuffle_AW>(a), other.Rows[3].mIntrin, r);
		result.Rows[i].mIntrin = r;
	}

	return result;
}

FVec4 FMat4::operator*(const FVec4 vec) const
{
	__m128 v = vec.mIntrin;
	__m128 v0 = simd::Permute4<simd::Shuffle_AX>(v);
	__m128 v1 = simd::Permute4<simd::Shuffle_AY>(v);
	__m128 v2 = simd::Permute4<simd::Shuffle_AZ>(v);
	__m128 v3 = simd::Permute4<simd::Shuffle_AW>(v);

	__m128 result = _mm_mul_ps(Rows[0].mIntrin, v0);
	result = _mm_fmadd_ps(Rows[1].mIntrin, v1, result);
	result = _mm_fmadd_ps(Rows[2].mIntrin, v2, result);
	result = _mm_fmadd_ps(Rows[3].mIntrin, v3, result);

	return FVec4(result);
}


FMat4::FMat4(float data[4][4]) noexcept
{
	Rows[0].mIntrin = _mm_loadu_ps(data[0]);
	Rows[1].mIntrin = _mm_loadu_ps(data[1]);
	Rows[2].mIntrin = _mm_loadu_ps(data[2]);
	Rows[3].mIntrin = _mm_loadu_ps(data[3]);
}

FMat4 FMat4::GetWithoutTranslation() const
{
	FMat4 mat = *this;
	mat.Rows[3].Set(0.0f, 0.0f, 0.0f, 1.0f);

	return mat;
}

void FMat4::TransposeTo(FMat4& other) const
{
	__m256 ahalf0 = _mm256_castps128_ps256(Rows[0].mIntrin);
	ahalf0 = _mm256_insertf128_ps(ahalf0, Rows[1].mIntrin, 1);
	__m256 ahalf1 = _mm256_castps128_ps256(Rows[2].mIntrin);
	ahalf1 = _mm256_insertf128_ps(ahalf1, Rows[3].mIntrin, 1);

	__m256 temp0 = _mm256_unpacklo_ps(ahalf0, ahalf1);
	__m256 temp1 = _mm256_unpackhi_ps(ahalf0, ahalf1);

	ahalf0 = _mm256_permute2f128_ps(temp0, temp1, 0x20);
	ahalf1 = _mm256_permute2f128_ps(temp0, temp1, 0x31);

	temp0 = _mm256_unpacklo_ps(ahalf0, ahalf1);
	temp1 = _mm256_unpackhi_ps(ahalf0, ahalf1);

	ahalf0 = _mm256_permute2f128_ps(temp0, temp1, 0x20);
	ahalf1 = _mm256_permute2f128_ps(temp0, temp1, 0x31);

	other.Rows[0].mIntrin = _mm256_castps256_ps128(ahalf0);
	other.Rows[1].mIntrin = _mm256_extractf128_ps(ahalf0, 1);
	other.Rows[2].mIntrin = _mm256_castps256_ps128(ahalf1);
	other.Rows[3].mIntrin = _mm256_extractf128_ps(ahalf1, 1);
}

FMat4 FMat4::Transposed() const
{
	__m256 ahalf0 = _mm256_castps128_ps256(Rows[0].mIntrin);
	ahalf0 = _mm256_insertf128_ps(ahalf0, Rows[1].mIntrin, 1);
	__m256 ahalf1 = _mm256_castps128_ps256(Rows[2].mIntrin);
	ahalf1 = _mm256_insertf128_ps(ahalf1, Rows[3].mIntrin, 1);

	__m256 temp0 = _mm256_unpacklo_ps(ahalf0, ahalf1);
	__m256 temp1 = _mm256_unpackhi_ps(ahalf0, ahalf1);

	ahalf0 = _mm256_permute2f128_ps(temp0, temp1, 0x20);
	ahalf1 = _mm256_permute2f128_ps(temp0, temp1, 0x31);

	temp0 = _mm256_unpacklo_ps(ahalf0, ahalf1);
	temp1 = _mm256_unpackhi_ps(ahalf0, ahalf1);

	ahalf0 = _mm256_permute2f128_ps(temp0, temp1, 0x20);
	ahalf1 = _mm256_permute2f128_ps(temp0, temp1, 0x31);

	FMat4 result;
	result.Rows[0].mIntrin = _mm256_castps256_ps128(ahalf0);
	result.Rows[1].mIntrin = _mm256_extractf128_ps(ahalf0, 1);
	result.Rows[2].mIntrin = _mm256_castps256_ps128(ahalf1);
	result.Rows[3].mIntrin = _mm256_extractf128_ps(ahalf1, 1);

	return result;
}

FMat4 FMat4::TransposeMat3() { return Transposed(); }

void FMat4::CopyAsMat3To(float* dest) const { memcpy(dest, RawData, sizeof(float32) * 12); }

void FMat4::LookAt(const FVec3 eye, const FVec3 target, const FVec3 upvec)
{
	FVec3 forward = (target - eye);
	forward.NormalizeIP();

	FVec3 right = upvec.Cross(forward);
	right.NormalizeIP();

	const FVec3 up = forward.Cross(right);

	Rows[0].Set(right.X, up.X, forward.X, 0.0f);
	Rows[1].Set(right.Y, up.Y, forward.Y, 0.0f);
	Rows[2].Set(right.Z, up.Z, forward.Z, 0.0f);
	Rows[3].Set(-eye.Dot(right), -eye.Dot(up), -eye.Dot(forward), 1.0f);
}

} // namespace fx
#endif
