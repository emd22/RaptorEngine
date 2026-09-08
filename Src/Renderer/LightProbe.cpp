/*
 * File:        LightProbe.cpp
 * Description: MVP light probe manager (SH L2 diffuse irradiance).
 */

#include "LightProbe.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

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
