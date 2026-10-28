#pragma once

#include <Core/Types.hpp>
#include <bit>
#include <format>
#include <type_traits>

#ifdef FX_USE_NEON
#include <arm_neon.h>
#elif defined(FX_USE_AVX)
#include "SSE.hpp"
#endif

namespace fx {

#if defined(FX_PLATFORM_MACOS)
#define FX_HAS_NATIVE_FLOAT16 1
using FLOAT16 = _Float16;
#else
using FLOAT16 = uint16;
#endif

#if defined(FX_USE_AVX) && (defined(__F16C__) || defined(FX_COMPILER_MSVC) || defined(FX_USE_SIMDE))
#define FX_USE_F16C 1
#endif

static_assert(sizeof(FLOAT16) == sizeof(uint16));

/**
 * @brief Convert float32 to half float the uint16 way. This is only used on platforms where _Float16 is not directly
 * supported (MSVC.. cough cough). The 99 percentile here would be running the fast path, but there is a fallback just
 * in case.
 */
constexpr uint16 FloatToHalfBits(float32 value)
{
#ifdef FX_USE_F16C
	if (!std::is_constant_evaluated()) {
		return static_cast<uint16>(_mm_cvtsi128_si32(_mm_cvtps_ph(_mm_set_ss(value), _MM_FROUND_TO_NEAREST_INT)));
	}
#endif

	const uint32 bits = std::bit_cast<uint32>(value);
	const uint16 sign = static_cast<uint16>((bits >> 16) & 0x8000);
	const uint32 magnitude = bits & 0x7FFFFFFF;

	if (magnitude >= 0x47800000) {
		if (magnitude > 0x7F800000) {
			return sign | 0x7E00 | static_cast<uint16>((magnitude >> 13) & 0x3FF);
		}

		return sign | 0x7C00;
	}

	if (magnitude < 0x38800000) {
		constexpr uint32 scSubnormalMagic = 126u << 23;

		const float32 aligned = std::bit_cast<float32>(magnitude) + std::bit_cast<float32>(scSubnormalMagic);

		return sign | static_cast<uint16>(std::bit_cast<uint32>(aligned) - scSubnormalMagic);
	}

	const uint32 odd = (magnitude >> 13) & 1;

	return sign | static_cast<uint16>((magnitude - 0x38000000 + 0xFFF + odd) >> 13);
}

constexpr float32 HalfBitsToFloat(uint16 half)
{
#ifdef FX_USE_F16C
	if (!std::is_constant_evaluated()) {
		return _mm_cvtss_f32(_mm_cvtph_ps(_mm_cvtsi32_si128(half)));
	}
#endif

	const uint32 sign = static_cast<uint32>(half & 0x8000) << 16;
	const uint32 exponent = (half >> 10) & 0x1F;
	const uint32 mantissa = half & 0x3FF;

	if (exponent == 0x1F) {
		if (mantissa != 0) {
			return std::bit_cast<float32>(sign | 0x7FC00000 | (mantissa << 13));
		}

		return std::bit_cast<float32>(sign | 0x7F800000);
	}

	if (exponent == 0) {
		if (mantissa == 0) {
			return std::bit_cast<float32>(sign);
		}

		const uint32 shift = static_cast<uint32>(std::countl_zero(mantissa)) - 21;
		const uint32 normalized = (mantissa << shift) & 0x3FF;

		return std::bit_cast<float32>(sign | ((113 - shift) << 23) | (normalized << 13));
	}

	return std::bit_cast<float32>(sign | ((exponent + 112) << 23) | (mantissa << 13));
}

struct Float16
{
public:
	static const Float16 scZero;
	static const Float16 scOne;
	static const Float16 scMaxValue;
	static const Float16 scLowest;
	static const Float16 scMinNormal;
	static const Float16 scMinSubnormal;
	static const Float16 scEpsilon;
	static const Float16 scInfinity;
	static const Float16 scNaN;

public:
	constexpr Float16() = default;

	constexpr explicit Float16(float32 value) : Value(FromFloat32(value)) {}

	static constexpr Float16 FromBits(uint16 bits)
	{
		Float16 result;
		result.Value = std::bit_cast<FLOAT16>(bits);

		return result;
	}

	constexpr uint16 GetBits() const { return std::bit_cast<uint16>(Value); }

	constexpr float32 ToFloat() const
	{
#ifdef FX_HAS_NATIVE_FLOAT16
		return static_cast<float32>(Value);
#else
		return HalfBitsToFloat(Value);
#endif
	}

	constexpr explicit operator float32() const { return ToFloat(); }

	constexpr bool IsNaN() const { return (GetBits() & 0x7FFF) > 0x7C00; }
	constexpr bool IsInfinite() const { return (GetBits() & 0x7FFF) == 0x7C00; }
	constexpr bool IsFinite() const { return (GetBits() & 0x7C00) != 0x7C00; }
	constexpr bool IsZero() const { return (GetBits() & 0x7FFF) == 0; }
	constexpr bool IsSubnormal() const { return (GetBits() & 0x7C00) == 0 && (GetBits() & 0x3FF) != 0; }

	constexpr bool IsNormal() const
	{
		const uint16 exponent = GetBits() & 0x7C00;
		return exponent != 0 && exponent != 0x7C00;
	}

	constexpr bool IsNegative() const { return (GetBits() & 0x8000) != 0; }

	constexpr Float16 Abs() const { return FromBits(GetBits() & 0x7FFF); }

	constexpr Float16 operator-() const { return FromBits(GetBits() ^ 0x8000); }
	constexpr Float16 operator+() const { return *this; }

	constexpr Float16 operator+(Float16 other) const { return FromNative(Native() + other.Native()); }
	constexpr Float16 operator-(Float16 other) const { return FromNative(Native() - other.Native()); }
	constexpr Float16 operator*(Float16 other) const { return FromNative(Native() * other.Native()); }
	constexpr Float16 operator/(Float16 other) const { return FromNative(Native() / other.Native()); }

	constexpr Float16& operator+=(Float16 other)
	{
		*this = *this + other;
		return *this;
	}

	constexpr Float16& operator-=(Float16 other)
	{
		*this = *this - other;
		return *this;
	}

	constexpr Float16& operator*=(Float16 other)
	{
		*this = *this * other;
		return *this;
	}

	constexpr Float16& operator/=(Float16 other)
	{
		*this = *this / other;
		return *this;
	}

	constexpr bool operator==(Float16 other) const { return Native() == other.Native(); }
	constexpr bool operator!=(Float16 other) const { return Native() != other.Native(); }
	constexpr bool operator<(Float16 other) const { return Native() < other.Native(); }
	constexpr bool operator<=(Float16 other) const { return Native() <= other.Native(); }
	constexpr bool operator>(Float16 other) const { return Native() > other.Native(); }
	constexpr bool operator>=(Float16 other) const { return Native() >= other.Native(); }

private:
#ifdef FX_HAS_NATIVE_FLOAT16
	/// The type used to compute values internally. All values are binned down to a 16 bit type afterwards.
	using NativeType = _Float16;

	static constexpr FLOAT16 FromFloat32(float32 value) { return static_cast<_Float16>(value); }
	constexpr NativeType Native() const { return Value; }
	static constexpr Float16 FromNative(NativeType value) { return FromBits(std::bit_cast<uint16>(value)); }
#else
	/// The type used to compute values internally. All values are binned down to a 16 bit type afterwards.
	using NativeType = float32;

	static constexpr FLOAT16 FromFloat32(float32 value) { return FloatToHalfBits(value); }
	constexpr NativeType Native() const { return HalfBitsToFloat(Value); }
	static constexpr Float16 FromNative(NativeType value) { return Float16(value); }
#endif

public:
	FLOAT16 Value {};
};

static_assert(sizeof(Float16) == sizeof(uint16));

inline constexpr Float16 Float16::scZero = Float16::FromBits(0x0000);
inline constexpr Float16 Float16::scOne = Float16::FromBits(0x3C00);
inline constexpr Float16 Float16::scMaxValue = Float16::FromBits(0x7BFF);
inline constexpr Float16 Float16::scLowest = Float16::FromBits(0xFBFF);
inline constexpr Float16 Float16::scMinNormal = Float16::FromBits(0x0400);
inline constexpr Float16 Float16::scMinSubnormal = Float16::FromBits(0x0001);
inline constexpr Float16 Float16::scEpsilon = Float16::FromBits(0x1400);
inline constexpr Float16 Float16::scInfinity = Float16::FromBits(0x7C00);
inline constexpr Float16 Float16::scNaN = Float16::FromBits(0x7E00);

constexpr uint32 PackFloat16x2(float32 low, float32 high)
{
	return static_cast<uint32>(Float16(low).GetBits()) | (static_cast<uint32>(Float16(high).GetBits()) << 16);
}

constexpr float32 UnpackFloat16Low(uint32 packed) { return Float16::FromBits(static_cast<uint16>(packed)).ToFloat(); }

constexpr float32 UnpackFloat16High(uint32 packed)
{
	return Float16::FromBits(static_cast<uint16>(packed >> 16)).ToFloat();
}

/**
 * @brief Converts a buffer of float32's to float16's.
 */
FX_FORCE_INLINE void ConvertToFloat16(const float32* src, Float16* dst, uint64 count)
{
	uint64 index = 0;

#ifdef FX_USE_NEON
	for (; index + 4 <= count; index += 4) {
		const float16x4_t halves = vcvt_f16_f32(vld1q_f32(src + index));
		vst1_u16(reinterpret_cast<uint16*>(dst + index), vreinterpret_u16_f16(halves));
	}
#elif defined(FX_USE_F16C)
	for (; index + 8 <= count; index += 8) {
		const __m128i halves = _mm256_cvtps_ph(_mm256_loadu_ps(src + index), _MM_FROUND_TO_NEAREST_INT);
		_mm_storeu_si128(reinterpret_cast<__m128i*>(dst + index), halves);
	}

	if (index + 4 <= count) {
		const __m128i halves = _mm_cvtps_ph(_mm_loadu_ps(src + index), _MM_FROUND_TO_NEAREST_INT);
		_mm_storel_epi64(reinterpret_cast<__m128i*>(dst + index), halves);
		index += 4;
	}
#endif

	for (; index < count; index++) {
		dst[index] = Float16(src[index]);
	}
}


/**
 * @brief Converts a buffer of float16's to float32's.
 */
FX_FORCE_INLINE void ConvertToFloat32(const Float16* src, float32* dst, uint64 count)
{
	uint64 index = 0;

#ifdef FX_USE_NEON
	for (; index + 4 <= count; index += 4) {
		const float16x4_t halves = vreinterpret_f16_u16(vld1_u16(reinterpret_cast<const uint16*>(src + index)));
		vst1q_f32(dst + index, vcvt_f32_f16(halves));
	}
#elif defined(FX_USE_F16C)
	for (; index + 8 <= count; index += 8) {
		const __m128i halves = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + index));
		_mm256_storeu_ps(dst + index, _mm256_cvtph_ps(halves));
	}

	if (index + 4 <= count) {
		const __m128i halves = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(src + index));
		_mm_storeu_ps(dst + index, _mm_cvtph_ps(halves));
		index += 4;
	}
#endif

	for (; index < count; index++) {
		dst[index] = src[index].ToFloat();
	}
}

} // namespace fx

template <>
struct std::formatter<fx::Float16> : std::formatter<fx::float32>
{
	auto format(fx::Float16 obj, std::format_context& ctx) const
	{
		return std::formatter<fx::float32>::format(obj.ToFloat(), ctx);
	}
};
