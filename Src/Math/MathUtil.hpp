#pragma once

#include <Core/Defines.hpp>
#include <Core/Types.hpp>
#include <bit>
#include <cmath>


namespace fx::MathUtil {

constexpr float32 DegreesToRadians(float32 degrees) { return ((degrees) * static_cast<float32>(M_PI / 180.0)); }
constexpr float32 RadiansToDegrees(float32 radians) { return ((radians) * static_cast<float32>(180.0 / M_PI)); }

constexpr float32 DegToRad(float32 degrees) { return ((degrees) * static_cast<float32>(M_PI / 180.0)); }
constexpr float32 RadToDeg(float32 radians) { return ((radians) * static_cast<float32>(180.0 / M_PI)); }

/**
 * @brief Reverses all bits in a 32-bit integer
 */
constexpr uint32 BitReverse32(uint32 bits)
{
	// Evil MSVC does not support __has_builtin

#if __has_builtin(__builtin_bitreverse32)
	// Use the intrinsic if we have it (supported in Clang and GCC)
	return __builtin_bitreverse32(bits);
#else
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	return ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
#endif
}


template <typename T>
	requires std::is_floating_point_v<T>
constexpr T RadicalInverse(const uint32 bits);

template <>
constexpr double RadicalInverse<double>(const uint32 bits)
{
	return static_cast<double>(BitReverse32(bits)) * (1.0 / 4294967296.0);
}

template <>
constexpr float32 RadicalInverse<float32>(const uint32 bits)
{
	return static_cast<float32>(BitReverse32(bits)) * 2.3283064365386963e-10f;
}


void SinCos(float32 rad, float32* out_sine, float32* out_cosine);

FX_FORCE_INLINE float32 RSqrt(float32 x);

FX_FORCE_INLINE float32 Clamp(float32 value, float32 lower, float32 upper) { return fmin(fmax(value, lower), upper); }

/**
 * @brief Cubic Hermite ease of `t` clamped to [0, 1].
 */
FX_FORCE_INLINE float32 SmoothStep(float32 t)
{
	const float32 x = Clamp(t, 0.0f, 1.0f);
	return x * x * (3.0f - 2.0f * x);
}

FX_FORCE_INLINE float32 SmoothInterpolate(float32 a, float32 b, float32 speed, float32 delta_time)
{
	return std::lerp(a, b, 1.0f - expf(-speed * delta_time));
}

FX_FORCE_INLINE uint64 AlignValue(uint64 value, const uint16 alignto)
{
	// Generic case
	const uint16 remainder = (value % alignto);

	// If the value is not aligned(there is a remainder in the division), offset our value by the missing bytes
	if (remainder != 0) {
		value += (alignto - remainder);
	}

	return value;
}

template <uint32 TAlignTo>
FX_FORCE_INLINE uint64 AlignValue(uint64 value)
{
	// Generic case

	const uint16 remainder = (value % TAlignTo);

	// If the value is not aligned(there is a remainder in the division), offset our value by the missing bytes
	if (remainder != 0) {
		value += (TAlignTo - remainder);
	}

	return value;
}

template <>
FX_FORCE_INLINE uint64 AlignValue<2>(uint64 value)
{
	// Get the bottom four bits (value % 16)
	const uint8 remainder = (value & 0x01);

	if (remainder != 0) {
		++value;
	}

	return value;
}


template <>
FX_FORCE_INLINE uint64 AlignValue<4>(uint64 value)
{
	// Get the bottom four bits (value % 16)
	const uint8 remainder = (value & 0x03);

	if (remainder != 0) {
		value += (4 - remainder);
	}

	return value;
}


template <>
FX_FORCE_INLINE uint64 AlignValue<8>(uint64 value)
{
	// Get the bottom four bits (value % 16)
	const uint8 remainder = (value & 0x07);

	if (remainder != 0) {
		value += (8 - remainder);
	}

	return value;
}

template <>
FX_FORCE_INLINE uint64 AlignValue<16>(uint64 value)
{
	// Get the bottom four bits (value % 16)
	const uint64 remainder = (value & 0x0F);

	if (remainder != 0) {
		value += (16 - remainder);
	}

	return value;
}

template <>
FX_FORCE_INLINE uint64 AlignValue<32>(uint64 value)
{
	// Get the bottom four bits (value % 32)
	const uint8 remainder = (value & 0x1F);

	if (remainder != 0) {
		value += (32 - remainder);
	}

	return value;
}

template <typename TPtrType, uint32 TAlignTo>
FX_FORCE_INLINE TPtrType AlignPtr(TPtrType ptr)
{
	return reinterpret_cast<TPtrType>(MathUtil::AlignValue<TAlignTo>(reinterpret_cast<uintptr_t>(ptr)));
}

template <typename TPtrType>
FX_FORCE_INLINE TPtrType AlignPtr(TPtrType ptr, uint32 alignto)
{
	return reinterpret_cast<TPtrType>(MathUtil::AlignValue(reinterpret_cast<uintptr_t>(ptr), alignto));
}

FX_FORCE_INLINE bool IsCloseTo(float32 value, float32 goal, float32 tolerance = 0.0001)
{
	return (std::abs(value - goal) >= tolerance);
}

/**
 * @brief Gets the sign of a floating point value, returning either 1.0 if positive, or -1.0 if negative.
 */
FX_FORCE_INLINE constexpr float32 GetSign(float32 x)
{
	constexpr uint32 scSignMask = 0x80000000U;
	constexpr uint32 scOne = std::bit_cast<uint32>(1.0f);

	const uint32 sign = (std::bit_cast<uint32>(x) & scSignMask);

	return std::bit_cast<float32>(scOne | sign);
}


} // namespace fx::MathUtil

#include "MathUtil.inl"
