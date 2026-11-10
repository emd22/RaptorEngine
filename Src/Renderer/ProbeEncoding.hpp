/*
 * File:        ProbeEncoding.hpp
 * Author:      emd22
 * Created:     21/10/2026
 * Description: The baked probe layout (SH and depth moments) to be shared between compiler/runtime baker.
 */

#pragma once

#include <Core/Types.hpp>
#include <Math/FVec3.hpp>
#include <Math/SIMDHelper.hpp>
#include <Math/Vec2.hpp>
#include <Renderer/Limits.hpp>
#include <algorithm>
#include <cmath>

namespace fx::ProbeEncoding {

constexpr float32 scSHNorm0 = 0.282095f;
constexpr float32 scSHNorm1 = 0.488603f;

/// A_l / pi per band, so evaluating the SH gives irradiance / pi
constexpr float32 scCosineLobe[Limits::ProbeSHCoeffCount] = {
	1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f,
};

constexpr uint32 scMomentTexelCount = Limits::ProbeDepthTexelCount;

/// Order must match EvalProbeIrradiance() in Shaders/ProbeCommon.hlsli
inline void EvalSHBasis(const FVec3 d, float32 out[Limits::ProbeSHCoeffCount])
{
	constexpr float32 scSHNorm2 = 1.092548f;
	constexpr float32 scSHNorm20 = 0.315392f;
	constexpr float32 scSHNorm22 = 0.546274f;

	out[0] = scSHNorm0;
	out[1] = scSHNorm1 * d.Y;
	out[2] = scSHNorm1 * d.Z;
	out[3] = scSHNorm1 * d.X;
	out[4] = scSHNorm2 * d.X * d.Y;
	out[5] = scSHNorm2 * d.Y * d.Z;
	out[6] = scSHNorm20 * (3.0f * d.Z * d.Z - 1.0f);
	out[7] = scSHNorm2 * d.X * d.Z;
	out[8] = scSHNorm22 * (d.X * d.X - d.Y * d.Y);
}

/**
 * @brief Octahedral map coordinates in [-1, 1] for direction `d`
 */
inline Vec2f DirectionToOctahedral(const FVec3 d)
{
	const float l1 = simd::HAdd(simd::Abs(d.mIntrin));

	const float32 inv_l1 = 1.0f / (std::fabs(d.X) + std::fabs(d.Y) + std::fabs(d.Z));

	FVec3 xyz = d * inv_l1;

	if (d.Y >= 0.0f) {
		return Vec2f(xyz.X, xyz.Z);
	}

	Vec2f sign = Vec2f(1.0f);

	if (xyz.X < 0.0f) {
		sign.X = -1.0f;
	}

	if (xyz.Z < 0.0f) {
		sign.Y = -1.0f;
	}

	const Vec2f result = Vec2f(1.0f) - Vec2f(std::fabs(xyz.Z), std::fabs(xyz.X));

	return (result * sign);

	// out_u = (1.0f - std::fabs(z)) * sign_x;
	// out_v = (1.0f - std::fabs(x)) * sign_z;
}

inline uint32 DirectionToMomentTexel(const FVec3 d)
{
	constexpr uint32 cSize = Limits::ProbeDepthSize;

	Vec2f uv = DirectionToOctahedral(d);

	const auto to_texel = [](float32 coord)
	{ return std::min(static_cast<uint32>(std::max((coord * 0.5f + 0.5f) * cSize, 0.0f)), cSize - 1); };

	return to_texel(uv.Y) * cSize + to_texel(uv.X);
}

/// Moment texel that texel (x, y) of a probe's atlas tile holds
inline uint32 TileTexelToMomentTexel(uint32 x, uint32 y)
{
	constexpr int32 cSize = static_cast<int32>(Limits::ProbeDepthSize);

	int32 u = static_cast<int32>(x) - 1;
	int32 v = static_cast<int32>(y) - 1;

	if (u < 0 || u >= cSize) {
		u = std::clamp(u, 0, cSize - 1);
		v = cSize - 1 - v;
	}

	if (v < 0 || v >= cSize) {
		v = std::clamp(v, 0, cSize - 1);
		u = cSize - 1 - u;
	}

	return static_cast<uint32>(v * cSize + u);
}

inline uint16 EncodeUNorm16(float32 value)
{
	return static_cast<uint16>(std::clamp(value, 0.0f, 1.0f) * 65535.0f + 0.5f);
}

inline float32 DecodeUNorm16(uint16 value) { return static_cast<float32>(value) / 65535.0f; }

} // namespace fx::ProbeEncoding
