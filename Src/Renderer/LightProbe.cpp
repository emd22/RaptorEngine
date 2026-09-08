/*
 * File:        LightProbe.cpp
 * Description: MVP light probe manager (SH L2 diffuse irradiance).
 */

#include "LightProbe.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

#include <cmath>
#include <cstring>

namespace fx {

// SH normalization constants (Peter-Pike Sloan). Basis order must match
// EvalProbeIrradiance() in Shaders/ProbeCommon.hlsli.
static constexpr float32 scY00 = 0.282095f;
static constexpr float32 scY1 = 0.488603f;

ProbeData MakeUniformAmbientProbe(float32 r, float32 g, float32 b)
{
	ProbeData probe {};
	// Constant irradiance E(n) = A  =>  L00 = A / Y00, all other coeffs zero.
	probe.SH[0][0] = r / scY00;
	probe.SH[0][1] = g / scY00;
	probe.SH[0][2] = b / scY00;
	return probe;
}

ProbeData MakeSkyGradientProbe(const float32 sky[3], const float32 ground[3])
{
	ProbeData probe {};
	// E(n) ~= avg + slope * n.y, with avg = (sky+ground)/2, slope = (sky-ground)/2.
	// L00 = avg / Y00, L(Y1-1, the y-basis coeff at index 1) = slope / 0.488603.
	for (uint32 c = 0; c < 3; c++) {
		const float32 avg = (sky[c] + ground[c]) * 0.5f;
		const float32 slope = (sky[c] - ground[c]) * 0.5f;
		probe.SH[0][c] = avg / scY00;
		probe.SH[1][c] = slope / scY1;
	}
	return probe;
}

void ProbeManager::Create()
{
	if (mbInitialized) {
		return;
	}

	// Default reproduces the old flat ambient (10,10,10 sRGB-ish) with a faint
	// cool sky / dark ground gradient so probe evaluation is visibly directional.
	const float32 sky[3] = { 0.055f, 0.062f, 0.075f };
	const float32 ground[3] = { 0.025f, 0.022f, 0.020f };
	mProbes[0] = MakeSkyGradientProbe(sky, ground);
	mProbeCount = 1;

	UploadToGpu();

	mbInitialized = true;
}

void ProbeManager::Destroy() { mbInitialized = false; }

void ProbeManager::SetUniformAmbient(float32 r, float32 g, float32 b)
{
	mProbes[0] = MakeUniformAmbientProbe(r, g, b);
	mProbeCount = 1;
	UploadToGpu();
}

void ProbeManager::SetSkyGradient(const float32 sky[3], const float32 ground[3])
{
	mProbes[0] = MakeSkyGradientProbe(sky, ground);
	mProbeCount = 1;
	UploadToGpu();
}

/// SH basis matching EvalProbeIrradiance() in Shaders/ProbeCommon.hlsli.
/// Order: Y00, Y1-1(y), Y10(z), Y11(x), Y2-2(xy), Y2-1(yz), Y20, Y21(xz), Y22.
static void ProbeBasisSH(float32 x, float32 y, float32 z, float32 out_basis[9])
{
	out_basis[0] = 0.282095f;
	out_basis[1] = 0.488603f * y;
	out_basis[2] = 0.488603f * z;
	out_basis[3] = 0.488603f * x;
	out_basis[4] = 1.092548f * x * y;
	out_basis[5] = 1.092548f * y * z;
	out_basis[6] = 0.315392f * (3.0f * z * z - 1.0f);
	out_basis[7] = 1.092548f * x * z;
	out_basis[8] = 0.546274f * (x * x - y * y);
}

void ProbeManager::BakeFromSceneLights(const Vec3f& sunDir, const float32 sunRGB[3], const float32 ambRGB[3])
{
	// Fibonacci sphere sampling for an even directional distribution.
	static constexpr uint32 scSampleCount = 1024;
	static constexpr float32 scGoldenAngle = 2.3999632f;
	static constexpr float32 scFourPiOverN = (4.0f * 3.14159265f) / static_cast<float32>(scSampleCount);

	const Vec3f sun = sunDir.Normalize();

	float32 sh[Limits::ProbeSHCoeffCount][3] = {};

	for (uint32 i = 0; i < scSampleCount; i++) {
		const float32 y = 1.0f - (2.0f * (static_cast<float32>(i) + 0.5f) / static_cast<float32>(scSampleCount));
		const float32 r = sqrtf(fmaxf(1.0f - y * y, 0.0f));
		const float32 phi = static_cast<float32>(i) * scGoldenAngle;

		const float32 x = r * cosf(phi);
		const float32 z = r * sinf(phi);

		const float32 n_dot_l = fmaxf(x * sun.X + y * sun.Y + z * sun.Z, 0.0f);

		float32 basis[Limits::ProbeSHCoeffCount];
		ProbeBasisSH(x, y, z, basis);

		for (uint32 k = 0; k < Limits::ProbeSHCoeffCount; k++) {
			for (uint32 c = 0; c < 3; c++) {
				sh[k][c] += (ambRGB[c] + sunRGB[c] * n_dot_l) * basis[k];
			}
		}
	}

	ProbeData& probe = mProbes[0];
	for (uint32 k = 0; k < Limits::ProbeSHCoeffCount; k++) {
		for (uint32 c = 0; c < 3; c++) {
			probe.SH[k][c] = sh[k][c] * scFourPiOverN;
		}
		probe.SH[k][3] = 0.0f;
	}

	mProbeCount = 1;
	UploadToGpu();
}

void ProbeManager::UploadToGpu()
{
	if (renderer::gGraphics == nullptr || renderer::gGraphics->ProbeBuffer.Buffer == nullptr) {
		return;
	}

	uint8* base = static_cast<uint8*>(renderer::gGraphics->ProbeBuffer.pMappedBuffer);
	if (base == nullptr) {
		return;
	}

	const uint32 page_size = renderer::gGraphics->ProbePageSize;
	const uint32 data_size = sizeof(ProbeData) * Limits::MaxIrradianceProbes;

	for (uint32 frame = 0; frame < renderer::FramesInFlight; frame++) {
		memcpy(base + page_size * frame, mProbes, data_size);
	}

	renderer::gGraphics->ProbeBuffer.FlushToGpu(0, page_size * renderer::FramesInFlight);
}

} // namespace fx
