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
#include <Renderer/Backend/Commands.hpp>
#include <Renderer/Backend/GpuBuffer.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/Limits.hpp>
#include <Renderer/RenderStage.hpp>

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
	/// Face resolution for capture bakes. 64px is plenty for L2 irradiance.
	static constexpr uint32 scCaptureSize = 64;
	static constexpr uint32 scCaptureFaces = 6;

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

	///////////////////////////////////
	// Cubemap capture bake
	///////////////////////////////////

	/// Arms a capture bake at `position`. The 6 faces render inside the next
	/// frame (see World::RenderProbeCapture), then FinishCaptureBake() reads
	/// back and projects. Must be called outside of frame recording.
	void BeginCaptureBake(const Vec3f& position);

	bool IsCapturePending() const { return mbCapturePending; }
	bool IsCaptureReady() const { return mbCaptureReady; }
	const Vec3f& GetCapturePosition() const { return mCapturePosition; }

	void EnsureCaptureStage();
	renderer::RenderStage& GetCaptureStage() { return mCaptureStage; }

	void SetCaptureCamera(uint32 face, const PerspectiveCamera& cam) { mFaceCameras[face] = cam; }

	/// Copies the capture color target into the face staging buffer. Must be
	/// called inside frame recording, after the capture stage has ended.
	void CopyCaptureFaceToStaging(renderer::CommandBuffer& cmd, uint32 face);

	/// Called once all faces + copies are recorded for this frame.
	void MarkCaptureReady()
	{
		mbCapturePending = false;
		mbCaptureReady = true;
	}

	/**
	 * @brief Blocks until the GPU is idle, reads back the 6 staged faces and
	 * projects captured radiance into probe 0. Returns false on failure.
	 * Must be called after the capture frame has been presented.
	 */
	bool FinishCaptureBake();

	/// Uploads all CPU probes to every in-flight page of the GPU probe buffer.
	void UploadToGpu();

private:
	ProbeData mProbes[Limits::MaxIrradianceProbes] {};
	uint32 mProbeCount = 1;
	bool mbInitialized = false;

	/// Capture bake state + resources (built lazily on first capture bake).
	Vec3f mCapturePosition = Vec3f::sZero;
	bool mbCapturePending = false;
	bool mbCaptureReady = false;
	bool mbCaptureBuilt = false;

	renderer::RenderStage mCaptureStage;
	renderer::RawGpuBuffer mCaptureStaging[scCaptureFaces];
	PerspectiveCamera mFaceCameras[scCaptureFaces];
};

extern ProbeManager* gProbeManager;

} // namespace fx
