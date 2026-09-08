/*
 * File:        LightProbe.hpp
 * Description: MVP light probes for precomputed global illumination.
 *
 * Stores diffuse irradiance as 2nd-order spherical harmonics (9 coeffs x RGB).
 * The MVP holds a single global probe (index 0) that replaces the flat ambient
 * term in Forward.hlsl. The CPU store + GPU buffer are sized for
 * Limits::MaxIrradianceProbes so a spatial probe volume can follow without
 * changing descriptor layouts.
 *
 * GPU mirror: `struct ProbeData { float4 SH[9]; }` in Shaders/ProbeCommon.hlsli.
 * float4 (not float3) is used deliberately so CPU/GPU packing matches exactly.
 */

#pragma once

#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <Renderer/Limits.hpp>

namespace fx {

struct ProbeData
{
	/// SH coefficients in D3D order: Y00, Y1-1, Y10, Y11, Y2-2, Y2-1, Y20, Y21, Y22.
	/// Only .rgb is used; .a is padding for GPU alignment.
	float32 SH[Limits::ProbeSHCoeffCount][4];
};

static_assert(sizeof(ProbeData) == Limits::ProbeSHCoeffCount * 4 * sizeof(float32),
			  "ProbeData must be tightly packed as 9 float4s to mirror the HLSL struct");

/// Builds SH coeffs for a constant irradiance colour (matches the old flat ambient).
ProbeData MakeUniformAmbientProbe(float32 r, float32 g, float32 b);

/// Builds SH coeffs for a vertical sky/ground gradient (L00 + L10 terms).
ProbeData MakeSkyGradientProbe(const float32 sky[3], const float32 ground[3]);

class ProbeManager
{
public:
	void Create();
	void Destroy();

	/// Number of valid probes in mProbes. MVP: always 1 (global probe at index 0).
	uint32 GetProbeCount() const { return mProbeCount; }

	ProbeData* GetProbes() { return mProbes; }

	void SetUniformAmbient(float32 r, float32 g, float32 b);
	void SetSkyGradient(const float32 sky[3], const float32 ground[3]);

	/**
	 * @brief Bakes probe 0 from analytic scene lights (no occlusion).
	 * Incident radiance is E(d) = ambient + sun * max(dot(d, sunDir), 0),
	 * numerically projected onto the SH basis. Matches EvalProbeIrradiance().
	 */
	void BakeFromSceneLights(const Vec3f& sunDir, const float32 sunRGB[3], const float32 ambRGB[3]);

	/// Uploads all CPU probes to every in-flight page of the GPU probe buffer.
	void UploadToGpu();

private:
	ProbeData mProbes[Limits::MaxIrradianceProbes] {};
	uint32 mProbeCount = 1;
	bool mbInitialized = false;
};

extern ProbeManager* gProbeManager;

} // namespace fx
