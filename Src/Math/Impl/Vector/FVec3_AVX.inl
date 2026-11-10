#pragma once

#include <Core/Defines.hpp>

#ifdef FX_USE_AVX
#include <Math/FVec3.hpp>
#include <Math/SIMDHelper.hpp>
#include <Math/SSE.hpp>

namespace fx {

FX_FORCE_INLINE FVec3::FVec3(float32 x, float32 y, float32 z, float32 w)
{
	const float32 values alignas(16)[4] = { x, y, z, w };
	mIntrin = _mm_load_ps(values);
}

FX_FORCE_INLINE FVec3::FVec3(const float32* unaligned)
{
	// Allocate here to avoid unordered loads into our SSE register and avoid overstepping the buffer
	const float32 values alignas(16)[4] = { unaligned[0], unaligned[1], unaligned[2], 0 };
	mIntrin = _mm_load_ps(values);
}

FX_FORCE_INLINE FVec3::FVec3(float32 scalar) { mIntrin = _mm_blend_ps(_mm_set1_ps(scalar), _mm_setzero_ps(), 0b1000); }

FX_FORCE_INLINE bool FVec3::IsCloseTo(const FVec3 other, const float32 tolerance) const
{
	return IsCloseTo(other.mIntrin, tolerance);
}

FX_FORCE_INLINE FVec3 FVec3::MulAdd(const FVec3 a, const FVec3 b, const FVec3 accum)
{
	return FVec3(_mm_fmadd_ps(a.mIntrin, b.mIntrin, accum.mIntrin));
}

FX_FORCE_INLINE bool FVec3::IsCloseTo(const FLOAT4 other, const float32 tolerance) const
{
	// Get the absolute difference
	const __m128 diff = SSE::RemoveSign(_mm_sub_ps(mIntrin, other));

	// Check if any absolute difference is > tolerance
	__m128i cmp_v = _mm_castps_si128(_mm_cmpgt_ps(diff, _mm_set1_ps(tolerance)));

	// If any components are, return true
	return static_cast<bool>(_mm_testz_si128(cmp_v, cmp_v));
}

FX_FORCE_INLINE float32 FVec3::Dot(const FVec3 other) const { return Dot(other.mIntrin); }

FX_FORCE_INLINE FVec3 FVec3::Cross(const FVec3 other) const { return FVec3(SSE::Cross(mIntrin, other.mIntrin)); }

FX_FORCE_INLINE bool FVec3::IsNearZero(const float32 tolerance) const { return IsCloseTo(sZero, tolerance); }

FX_FORCE_INLINE void FVec3::Set(float32 x, float32 y, float32 z) { mIntrin = _mm_setr_ps(x, y, z, 0.0f); }

FX_FORCE_INLINE float32 FVec3::Dot(FLOAT4 other) const
{
	// Mask is Src->0111 Dest->0111 so we do not include the unused component in our result
	// This is likely already handled by the zero during initalization (and the _mm_blend when initializing with
	// scalar), but better to be safe.
	return _mm_cvtss_f32(_mm_dp_ps(mIntrin, other, 0b01110111));
}

FX_FORCE_INLINE FVec3 FVec3::Min(const FVec3 a, const FVec3 b) { return FVec3(_mm_min_ps(a.mIntrin, b.mIntrin)); }

FX_FORCE_INLINE FVec3 FVec3::Max(const FVec3 a, const FVec3 b) { return FVec3(_mm_max_ps(a.mIntrin, b.mIntrin)); }

FX_FORCE_INLINE FVec3 FVec3::Clamp(const FVec3 v, const FVec3 min, const FVec3 max)
{
	return FVec3(_mm_min_ps(_mm_max_ps(v.mIntrin, min.mIntrin), max.mIntrin));
}

FX_FORCE_INLINE FVec3 FVec3::Lerp(const FVec3 a, const FVec3 b, const float f)
{
	// a + f * (b - a);
	__m128 d = _mm_sub_ps(b.mIntrin, a.mIntrin);
	const __m128 f_v = _mm_set1_ps(f);

	return FVec3(_mm_add_ps(a.mIntrin, _mm_mul_ps(d, f_v)));
}


FX_FORCE_INLINE FVec3& FVec3::LerpIP(const FVec3 dest, const float step)
{
	// a + f * (b - a);
	__m128 d = _mm_sub_ps(dest.mIntrin, mIntrin);
	const __m128 f_v = _mm_set1_ps(step);

	mIntrin = _mm_add_ps(mIntrin, _mm_mul_ps(d, f_v));

	return *this;
}

FX_FORCE_INLINE FVec3& FVec3::NormalizeIP()
{
	const float32 length = Length();

	if (length == 0.0f) {
		return *this;
	}

	// Calculate length and splat to register
	const __m128 len_v = _mm_set1_ps(Length());

	// Divide vector by length
	mIntrin = _mm_div_ps(mIntrin, len_v);

	return *this;
}

FX_FORCE_INLINE FVec3 FVec3::Normalize() const
{
	const float32 length = Length();

	if (length == 0.0f) {
		return *this;
	}

	const __m128 len_v = _mm_set1_ps(length);

	return FVec3(_mm_div_ps(mIntrin, len_v));
}


FX_FORCE_INLINE FVec3 FVec3::SafeNormalize(const FVec3 v, const FVec3 fallback) const
{
	static constexpr float32 scEpsilon = 1e-5f;

	const float32 len = Length();

	if (len < scEpsilon) {
		return fallback;
	}

	return FVec3(_mm_div_ps(mIntrin, _mm_set1_ps(len)));
}

FX_FORCE_INLINE float32 FVec3::Length() const
{
	__m128 v = mIntrin;
	return sqrtf(Dot(v));
}


//////////////////////////////
// Operator Overloads
//////////////////////////////

FX_FORCE_INLINE bool FVec3::operator==(const FVec3 other) const
{
	__m128i cmp_v = _mm_castps_si128(_mm_cmpeq_ps(mIntrin, other.mIntrin));
	// This is frequently done as a comparision against _mm_movemask, but _mm_test_all_ones potentially saves one extra
	// instruction.
	return static_cast<bool>(_mm_test_all_ones(cmp_v));
}

FX_FORCE_INLINE bool FVec3::operator>(const FVec3 other) const
{
	return (_mm_movemask_ps(_mm_cmpgt_ps(mIntrin, other.mIntrin)) & 0b0111) == 0b0111;
}

FX_FORCE_INLINE bool FVec3::operator<(const FVec3 other) const
{
	return (_mm_movemask_ps(_mm_cmplt_ps(mIntrin, other.mIntrin)) & 0b0111) == 0b0111;
}

FX_FORCE_INLINE bool FVec3::operator>=(const FVec3 other) const
{
	return (_mm_movemask_ps(_mm_cmpge_ps(mIntrin, other.mIntrin)) & 0b0111) == 0b0111;
}

FX_FORCE_INLINE bool FVec3::operator<=(const FVec3 other) const
{
	return (_mm_movemask_ps(_mm_cmple_ps(mIntrin, other.mIntrin)) & 0b0111) == 0b0111;
}

FX_FORCE_INLINE FVec3 FVec3::operator+(const FVec3 other) const
{
	__m128 result = _mm_add_ps(mIntrin, other.mIntrin);
	return FVec3(result);
}

FX_FORCE_INLINE FVec3 FVec3::operator-(const FVec3 other) const
{
	__m128 result = _mm_sub_ps(mIntrin, other.mIntrin);
	return FVec3(result);
}

FX_FORCE_INLINE FVec3 FVec3::operator*(const FVec3 other) const
{
	__m128 result = _mm_mul_ps(mIntrin, other.mIntrin);
	return FVec3(result);
}

FX_FORCE_INLINE FVec3 FVec3::operator/(const FVec3 other) const
{
	__m128 result = _mm_div_ps(mIntrin, other.mIntrin);
	return FVec3(result);
}

FX_FORCE_INLINE FVec3 FVec3::operator*(float32 scalar) const
{
	__m128 result = _mm_mul_ps(mIntrin, _mm_set1_ps(scalar));
	return FVec3(result);
}


FX_FORCE_INLINE FVec3 FVec3::operator/(float32 scalar) const
{
	__m128 result = _mm_div_ps(mIntrin, _mm_set1_ps(scalar));
	return FVec3(result);
}


FX_FORCE_INLINE FVec3 FVec3::operator-() const
{
	__m128 result = _mm_xor_ps(mIntrin, _mm_set1_ps(-0.0f));
	return FVec3(result);
}


FX_FORCE_INLINE FVec3& FVec3::operator+=(const FVec3 other)
{
	mIntrin = _mm_add_ps(mIntrin, other.mIntrin);
	return *this;
}

FX_FORCE_INLINE FVec3& FVec3::operator-=(const FVec3 other)
{
	mIntrin = _mm_sub_ps(mIntrin, other.mIntrin);
	return *this;
}

FX_FORCE_INLINE FVec3& FVec3::operator-=(float32 scalar)
{
	mIntrin = _mm_sub_ps(mIntrin, _mm_set1_ps(scalar));
	return *this;
}

FX_FORCE_INLINE FVec3& FVec3::operator*=(const FVec3 other)
{
	mIntrin = _mm_mul_ps(mIntrin, other.mIntrin);
	return *this;
}

} // namespace fx

#endif // #ifdef FX_USE_AVX
