#pragma once

#include <Core/Serialize.hpp>
#include <Core/Types.hpp>
#include <format>


#ifdef FX_USE_NEON
#include "NeonUtil.hpp"

#include <arm_neon.h>
#elif defined(FX_USE_AVX)
#include "SSE.hpp"
#include "SSEUtil.hpp"
#endif

namespace fx {

class FVec3;

class alignas(16) FVec4
{
private:
#if defined(FX_USE_NEON)
	using SimdType = float32x4_t;
#elif defined(FX_USE_AVX)
	using SimdType = __m128;
#endif

public:
	static const FVec4 sZero;
	static const FVec4 sOne;

public:
	friend class FMat4;

	FVec4() = default;
	FVec4(float32 x, float32 y, float32 z, float32 w);
	FVec4(const float32* values);
	/**
	 * @brief Create a Vec4 from an existing Vec3 and a W component.
	 */
	FVec4(const FVec3& xyz, float32 w);
	explicit FVec4(float32 scalar);

	FX_FORCE_INLINE void Set(float32 x, float32 y, float32 z, float32 w);
	FX_FORCE_INLINE void Set(float32 scalar);


	FX_FORCE_INLINE bool IsZero() const;
	FX_FORCE_INLINE bool IsNearZero(const float32 tolerance = 0.00001) const;
	FX_FORCE_INLINE bool IsCloseTo(const FVec4 other, const float32 tolerance = 0.00001) const;
#ifdef FX_USE_SIMD
	FX_FORCE_INLINE bool IsCloseTo(const SimdType other, const float32 tolerance = 0.00001) const;
#endif

	FVec4& operator=(const FVec4 other);

	FX_FORCE_INLINE FVec4 Normalize() const;
	FX_FORCE_INLINE FVec4& NormalizeIP();

	FX_FORCE_INLINE float32 LengthSquared() const;
	FX_FORCE_INLINE bool IsNormalized(float32 tolerance) const { return abs(LengthSquared() - 1.0f) <= tolerance; }


#ifdef FX_USE_NEON
#else
	FX_FORCE_INLINE float32 GetX() const { return X; }
	FX_FORCE_INLINE float32 GetY() const { return Y; }
	FX_FORCE_INLINE float32 GetZ() const { return Z; }
	FX_FORCE_INLINE float32 GetW() const { return W; }
#endif

#ifdef FX_USE_NEON
	template <int TX, int TY, int TZ, int TW>
	FX_FORCE_INLINE static FVec4 FlipSigns(const FVec4 vec)
	{
		return FVec4(Neon::FlipSigns<TX, TY, TZ, TW>(vec.mIntrin));
	}

#elif FX_USE_AVX
	template <int TX, int TY, int TZ, int TW>
	FX_FORCE_INLINE static FVec4 FlipSigns(const FVec4 vec)
	{
		return FVec4(SSE::FlipSigns<TX, TY, TZ, TW>(vec.mIntrin));
	}
#else
	template <int TX, int TY, int TZ, int TW>
	FX_FORCE_INLINE static vec4f FlipSigns(const FVec4 vec)
	{
		constexpr float rx = TX > 0.0 ? vec.X : -vec.X;
		constexpr float ry = TY > 0.0 ? vec.Y : -vec.Y;
		constexpr float rz = TZ > 0.0 ? vec.Z : -vec.Z;
		constexpr float rw = TW > 0.0 ? vec.W : -vec.W;

		return FVec4(rx, ry, rz, rw);
	}
#endif

	/////////////////////////////////////
	// Operator overloads
	/////////////////////////////////////

	FX_FORCE_INLINE FVec4 operator+(const FVec4 other) const;
	FX_FORCE_INLINE FVec4 operator-(const FVec4 other) const;
	FX_FORCE_INLINE FVec4 operator*(const FVec4 other) const;
	FX_FORCE_INLINE FVec4 operator/(const FVec4 other) const;

	FX_FORCE_INLINE FVec4 operator*(float32 scalar) const;
	FX_FORCE_INLINE FVec4 operator/(float32 scalar) const;

	FX_FORCE_INLINE bool operator>(const FVec4 other) const;
	FX_FORCE_INLINE bool operator<(const FVec4 other) const;
	FX_FORCE_INLINE bool operator>=(const FVec4 other) const;
	FX_FORCE_INLINE bool operator<=(const FVec4 other) const;

	FVec4 operator-() const;

	FVec4& operator+=(const FVec4 other);
	FVec4& operator-=(const FVec4 other);
	FVec4& operator*=(const FVec4 other);
	FX_FORCE_INLINE FVec4& operator*=(float32 scalar);


#ifdef FX_USE_SIMD
	explicit FVec4(SimdType intrin) : mIntrin(intrin) {}

	FVec4& operator=(SimdType other)
	{
		mIntrin = other;
		return *this;
	}
#endif

public:
#if FX_USE_SIMD
	union alignas(16)
	{
		SimdType mIntrin;
		float32 mData[4];
		struct
		{
			float32 X, Y, Z, W;
		};
	};
#else
	float32 X, Y, Z, W;
#endif
};


/////////////////////////////////////
// Integer vector classes
/////////////////////////////////////

class alignas(16) Vec4u
{
private:
#if defined(FX_USE_NEON)
	using SimdType = uint32x4_t;
#elif defined(FX_USE_AVX)
	using SimdType = __m128i;
#endif

public:
	static const Vec4u sZero;
	static const Vec4u sOne;

public:
	Vec4u() = default;
	Vec4u(uint32 x, uint32 y, uint32 z, uint32 w);
	Vec4u(const uint32* values);
	explicit Vec4u(uint32 scalar);

	FX_FORCE_INLINE void Set(uint32 x, uint32 y, uint32 z, uint32 w);
	FX_FORCE_INLINE void Set(uint32 scalar);

	FX_FORCE_INLINE bool IsZero() const;

	Vec4u& operator=(const Vec4u& other);

#ifdef FX_USE_NEON
	FX_FORCE_INLINE uint32 GetX() const { return vgetq_lane_u32(mIntrin, 0); }
	FX_FORCE_INLINE uint32 GetY() const { return vgetq_lane_u32(mIntrin, 1); }
	FX_FORCE_INLINE uint32 GetZ() const { return vgetq_lane_u32(mIntrin, 2); }
	FX_FORCE_INLINE uint32 GetW() const { return vgetq_lane_u32(mIntrin, 3); }
#else
	FX_FORCE_INLINE uint32 GetX() const { return X; }
	FX_FORCE_INLINE uint32 GetY() const { return Y; }
	FX_FORCE_INLINE uint32 GetZ() const { return Z; }
	FX_FORCE_INLINE uint32 GetW() const { return W; }
#endif

#ifdef FX_USE_NEON
	template <int TX, int TY, int TZ, int TW>
	FX_FORCE_INLINE static Vec4u FlipSigns(const Vec4u& vec)
	{
		return Vec4u(Neon::FlipSigns<TX, TY, TZ, TW>(vec.mIntrin));
	}

#elif FX_USE_AVX
	template <int TX, int TY, int TZ, int TW>
	FX_FORCE_INLINE static Vec4u FlipSigns(const Vec4u& vec)
	{
		return Vec4u(SSE::SetSigns<TX, TY, TZ, TW>(vec.mIntrin));
	}
#else
	template <int TX, int TY, int TZ, int TW>
	FX_FORCE_INLINE static Vec4u FlipSigns(const Vec4u& vec)
	{
		constexpr float rx = TX > 0.0 ? vec.X : -vec.X;
		constexpr float ry = TY > 0.0 ? vec.Y : -vec.Y;
		constexpr float rz = TZ > 0.0 ? vec.Z : -vec.Z;
		constexpr float rw = TW > 0.0 ? vec.W : -vec.W;

		return Vec4u(rx, ry, rz, rw);
	}
#endif

	/////////////////////////////////////
	// Operator overloads
	/////////////////////////////////////

	FX_FORCE_INLINE Vec4u operator+(const Vec4u& other) const;
	FX_FORCE_INLINE Vec4u operator-(const Vec4u& other) const;
	FX_FORCE_INLINE Vec4u operator*(const Vec4u& other) const;

	FX_FORCE_INLINE Vec4u operator*(uint32 scalar) const;

	Vec4u& operator+=(const Vec4u& other);
	Vec4u& operator-=(const Vec4u& other);
	Vec4u& operator*=(const Vec4u& other);
	FX_FORCE_INLINE Vec4u& operator*=(uint32 scalar);

#ifdef FX_USE_SIMD
	Vec4u& operator=(const SimdType other)
	{
		mIntrin = other;
		return *this;
	}

	explicit Vec4u(SimdType intrin) : mIntrin(intrin) {}
#endif

public:
#if FX_USE_SIMD
	union alignas(16)
	{
		SimdType mIntrin;
		uint32 mData[4];
		struct
		{
			uint32 X, Y, Z, W;
		};
	};
#else
	uint32 X, Y, Z, W;
#endif
};

} // namespace fx


RS_ARRAY_TYPE(fx::FVec4, fx::float32, 4)
RS_ARRAY_TYPE(fx::Vec4u, fx::uint32, 4)


template <>
struct std::formatter<fx::FVec4>
{
	auto parse(format_parse_context& ctx) { return ctx.begin(); }

	auto format(const fx::FVec4& obj, std::format_context& ctx) const
	{
		return std::format_to(ctx.out(), "({:.04}, {:.04}, {:.04}, {:.04})", obj.X, obj.Y, obj.Z, obj.W);
	}
};
template <>
struct std::formatter<fx::Vec4u>
{
	auto parse(format_parse_context& ctx) { return ctx.begin(); }

	auto format(const fx::Vec4u& obj, std::format_context& ctx) const
	{
		return std::format_to(ctx.out(), "({}, {}, {}, {})", obj.X, obj.Y, obj.Z, obj.W);
	}
};


/* Definitions for inline functions */
#include "Impl/Vector/FVec4_AVX.inl"
#include "Impl/Vector/FVec4_Fallback.inl"
#include "Impl/Vector/FVec4_Neon.inl"
