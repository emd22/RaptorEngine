#pragma once

#include "SIMDHelper.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Math/Real.h>

#include <Core/Defines.hpp>
#include <Core/Serialize.hpp>
#include <Core/Types.hpp>
#include <format>

#ifdef FX_USE_NEON
#include "NeonUtil.hpp"

#include <arm_neon.h>
#elif defined(FX_USE_AVX)
#include "SSEUtil.hpp"
#endif

namespace JPH {
class Vec3;
using RVec3 = Vec3;
} // namespace JPH


namespace fx {

class Vec4f;
class Quat;

class alignas(16) FVec3
{
private:
public:
	static const FVec3 sZero;
	static const FVec3 sOne;

	static const FVec3 sUp;
	static const FVec3 sRight;
	static const FVec3 sForward;

public:
	FVec3() = default;
	FX_FORCE_INLINE FVec3(float32 x, float32 y, float32 z) : FVec3(x, y, z, 0.0f) {}
	FX_FORCE_INLINE FVec3(float32 x, float32 y, float32 z, float32 w);
	FX_FORCE_INLINE FVec3(const float32* values);
	FVec3(const JPH::Vec3& other);

	FX_FORCE_INLINE explicit FVec3(float32 scalar);

	FX_FORCE_INLINE static FVec3 MulAdd(const FVec3 a, const FVec3 b, const FVec3 accum);

	static FVec3 GetSurfaceNormal(const FVec3 a, const FVec3 b, const FVec3 c)
	{
		FVec3 edge1 = b - a;
		const FVec3 edge2 = c - b;

		return FlipSigns<1, 1, 1, 0>(edge2.Cross(edge1)).NormalizeIP();
	}

#ifdef FX_USE_SIMD
	explicit FVec3(FLOAT4 intrin) : mIntrin(intrin) {}
	FVec3(const Vec4f& other);
#else
	FVec3(const Vec4f& other) : X(other.X), Y(other.Y), Z(other.Z) {}
#endif


	FX_FORCE_INLINE void Set(float32 x, float32 y, float32 z);

	void ToJoltVec3(JPH::RVec3& jolt_vec) const;
	void FromJoltVec3(const JPH::RVec3& jolt_vec);


	/**
	 * @brief Converts a Jolt Vec3 to a FVec3.
	 */
	static FVec3 FromJolt(const JPH::RVec3 jvec) { return FVec3(jvec.mValue); }

	/**
	 * @brief Converts a FVec3 to Jolt's FVec3 container.
	 */
	static JPH::RVec3 ToJolt(const FVec3 vec) { return JPH::Vec3(vec.mIntrin); }

	static FX_FORCE_INLINE FVec3 FromDifference(const float32* a, const float32* b) { return (FVec3(a) - FVec3(b)); }

	FX_FORCE_INLINE void CopyTo(float32* output)
	{
		output[0] = X;
		output[1] = Y;
		output[2] = Z;
	}

	FX_FORCE_INLINE float32 DistanceTo(const FVec3 other) const { return (other - *this).Length(); }

	FX_FORCE_INLINE bool IntersectsSphere(const FVec3 sphere_center, float32 sphere_radius) const
	{
		const FVec3 diff = (*this) - sphere_center;
		const float dist2 = diff.Dot(diff);

		return dist2 <= sphere_radius * sphere_radius;
	}


	FX_FORCE_INLINE bool operator==(const FVec3 other) const;
	bool operator==(const JPH::Vec3& other) const;

	FX_FORCE_INLINE bool operator>(const FVec3 other) const;
	FX_FORCE_INLINE bool operator<(const FVec3 other) const;
	FX_FORCE_INLINE bool operator>=(const FVec3 other) const;
	FX_FORCE_INLINE bool operator<=(const FVec3 other) const;

	FX_FORCE_INLINE bool IsZero() const;
	FX_FORCE_INLINE bool IsNearZero(const float32 tolerance = 0.00001) const;
	FX_FORCE_INLINE bool IsCloseTo(const FVec3 other, const float32 tolerance = 0.00001) const;
#ifdef FX_USE_SIMD
	FX_FORCE_INLINE bool IsCloseTo(const FLOAT4 other, const float32 tolerance = 0.00001) const;
#endif
	bool IsCloseTo(const JPH::Vec3& other, const float32 threshold = 0.001) const;

	FX_FORCE_INLINE FVec3 Normalize() const;


	/**
	 * Normalizes the vector in place (modifies the source vector.)
	 * @returns The modified vector.
	 */
	FX_FORCE_INLINE FVec3& NormalizeIP();

	FX_FORCE_INLINE FVec3 SafeNormalize(const FVec3 fallback) const;

	FX_FORCE_INLINE float32 Length() const;
	FX_FORCE_INLINE FVec3 Cross(const FVec3 other) const;
	FVec3 CrossSlow(const FVec3 other) const;

	FVec3 Rotate(const Quat rotation) const;

	FX_FORCE_INLINE float32 Dot(const FVec3 other) const;

#ifdef FX_USE_SIMD
	FX_FORCE_INLINE float32 Dot(FLOAT4 other) const;
#endif

	FX_FORCE_INLINE static FVec3 Min(const FVec3 a, const FVec3 b);
	FX_FORCE_INLINE static FVec3 Max(const FVec3 a, const FVec3 b);
	FX_FORCE_INLINE static FVec3 Clamp(const FVec3 v, const FVec3 min, const FVec3 max);
	FX_FORCE_INLINE static FVec3 Lerp(const FVec3 a, const FVec3 b, const float f);
	FX_FORCE_INLINE FVec3& LerpIP(const FVec3 dest, const float step);

	FX_FORCE_INLINE FVec3& SmoothInterpolate(const FVec3 dest, const float speed, const float delta_time)
	{
		LerpIP(dest, 1.0f - expf(-speed * delta_time));
		return *this;
	}

	void Print() const;

#ifdef FX_USE_NEON
	// Neon has optimized fetch's when using the intrinsic
	FX_FORCE_INLINE float32 GetX() const { return vgetq_lane_f32(mIntrin, 0); }
	FX_FORCE_INLINE float32 GetY() const { return vgetq_lane_f32(mIntrin, 1); }
	FX_FORCE_INLINE float32 GetZ() const { return vgetq_lane_f32(mIntrin, 2); }
#else
	FX_FORCE_INLINE float32 GetX() const { return X; }
	FX_FORCE_INLINE float32 GetY() const { return Y; }
	FX_FORCE_INLINE float32 GetZ() const { return Z; }
#endif

#ifdef FX_USE_NEON
	template <int TX, int TY, int TZ, int TW>
	FX_FORCE_INLINE static FVec3 FlipSigns(const FVec3 vec)
	{
		return FVec3(Neon::FlipSigns<TX, TY, TZ, TW>(vec.mIntrin));
	}

#elif FX_USE_AVX
	template <int TX, int TY, int TZ, int TW>
	FX_FORCE_INLINE static FVec3 FlipSigns(const FVec3 vec)
	{
		return FVec3(SSE::FlipSigns<TX, TY, TZ, TW>(vec.mIntrin));
	}
#else
	template <int TX, int TY, int TZ, int TW>
	FX_FORCE_INLINE static FVec3 FlipSigns(const FVec3 vec)
	{
		constexpr float rx = TX > 0 ? vec.X : -vec.X;
		constexpr float ry = TY > 0 ? vec.Y : -vec.Y;
		constexpr float rz = TZ > 0 ? vec.Z : -vec.Z;
		constexpr float rw = TW > 0 ? vec.W : -vec.W;

		return FVec3(rx, ry, rz, rw);
	}
#endif

	FX_FORCE_INLINE FVec3 Abs() const
	{
#ifdef FX_USE_NEON
		return FVec3(Neon::SetSigns<1>(mIntrin));
#elif FX_USE_AVX
		return FVec3(SSE::SetSigns<1>(mIntrin));
#endif
	}

	/////////////////////////////////////
	// Operator overloads
	/////////////////////////////////////

	FX_FORCE_INLINE FVec3 operator+(const FVec3 other) const;
	FX_FORCE_INLINE FVec3 operator-(const FVec3 other) const;
	FX_FORCE_INLINE FVec3 operator*(const FVec3 other) const;
	FX_FORCE_INLINE FVec3 operator/(const FVec3 other) const;

	FX_FORCE_INLINE FVec3 operator*(float32 scalar) const;
	FX_FORCE_INLINE FVec3 operator/(float32 scalar) const;

	FX_FORCE_INLINE FVec3 operator-() const;

	FX_FORCE_INLINE FVec3& operator+=(const FVec3 other);
	FX_FORCE_INLINE FVec3& operator-=(float32 scalar);
	FX_FORCE_INLINE FVec3& operator-=(const FVec3 other);
	FX_FORCE_INLINE FVec3& operator*=(const FVec3 other);

#ifdef FX_USE_SIMD

	FVec3& operator=(const FLOAT4 other)
	{
		mIntrin = other;
		return *this;
	}

#endif

public:
#if defined(FX_USE_SIMD)
	union alignas(16)
	{
		FLOAT4 mIntrin;
		float32 mData[4];

		struct
		{
			float32 X, Y, Z, W;
		};
	};
#else
	struct
	{
		float32 X, Y, Z;
	};
#endif
};

} // namespace fx


template <>
struct std::formatter<fx::FVec3>
{
	auto parse(format_parse_context& ctx) { return ctx.begin(); }

	auto format(const fx::FVec3& obj, std::format_context& ctx) const
	{
		return std::format_to(ctx.out(), "({:.06f}, {:.06f}, {:.06f})", obj.X, obj.Y, obj.Z);
	}
};

RS_ARRAY_TYPE(fx::FVec3, fx::float32, 3)


#ifdef FX_USE_AVX
#include "Impl/Vector/FVec3_AVX.inl"
#else
#include "Impl/Vector/FVec3_Neon.inl"
#endif
