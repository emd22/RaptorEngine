/*
 * File:        LightProbe.hpp
 * Description: Light probes for precomputed global illumination.
 *
 * Stores diffuse irradiance as 2nd-order spherical harmonics (9 coeffs x RGB).
 * Probes live on a 3D grid (default 4x2x4 = 32) auto-fitted to the level; the
 * shader trilinearly blends the 8 surrounding probes per pixel.
 *
 * GPU mirrors in Shaders/ProbeCommon.hlsli:
 *   struct ProbeData   { float4 SH[9]; }
 *   struct ProbeVolume { float4 Min; float4 InvCellSize; uint4 DimsAndCount; }
 * float4 (not float3) is used deliberately so CPU/GPU packing matches exactly.
 */

#pragma once

#include <Core/String.hpp>
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

/// Volume descriptor uploaded to the GPU for spatial probe lookup.
struct ProbeVolumeData
{
	float32 Min[4];
	float32 InvCellSize[4];
	uint32 DimsAndCount[4]; // Grid dims XYZ + probe count in W
};

static_assert(sizeof(ProbeVolumeData) == 48, "ProbeVolumeData must mirror the HLSL ProbeVolume struct");

/// World-space boxes used for probe placement (bounds fit + push-out).
struct ProbeBoxList
{
	struct Box
	{
		Vec3f Min;
		Vec3f Max;
	};

	static constexpr uint32 scMaxBoxes = 256;
	Box Boxes[scMaxBoxes];
	uint32 Count = 0;
	Vec3f Min;
	Vec3f Max;
	bool Any = false;
};

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

	/// Number of probes baked per frame during a grid bake (spreads the hitch).
	static constexpr uint32 scProbesPerFrame = 1;

public:
	void Create();
	void Destroy();

	ProbeData* GetProbes() { return mProbes; }

	const Vec3f* GetProbePositions() const { return mProbePositions; }
	uint32 GetProbeCount() const { return Limits::MaxIrradianceProbes; }
	uint32 GetCurrentProbeIndex() const { return mCurrentProbe; }
	const ProbeVolumeData& GetVolume() const { return mVolume; }

	/// Fills all probes
	void SetUniformAmbient(float32 r, float32 g, float32 b);
	void SetSkyGradient(const float32 sky[3], const float32 ground[3]);

	void BakeFromSceneLights(const Vec3f& sunDir, const float32 sunRGB[3], const float32 ambRGB[3]);

	///////////////////////////////////
	// Probe Cubemap
	///////////////////////////////////

	void BeginCaptureBake(const Vec3f& position);
	void BeginGridBake();

	void BeginGridBakeAt(const Vec3f& center, const Vec3f& size);

	bool IsCapturePending() const { return mbCapturePending; }
	bool IsCaptureReady() const { return mbCaptureReady; }

	const Vec3f& GetCapturePosition() const { return mProbePositions[mCurrentProbe]; }

	void EnsureCaptureStage();
	renderer::RenderStage& GetCaptureStage() { return mCaptureStage; }

	void SetCaptureCamera(uint32 face, const PerspectiveCamera& cam) { mFaceCameras[face] = cam; }

	void CopyCaptureFaceToStaging(renderer::CommandBuffer& cmd, uint32 face);

	/// Called once all faces + copies are recorded for this frame.
	void MarkCaptureReady()
	{
		mbCapturePending = false;
		mbCaptureReady = true;
	}

	bool FinishCaptureBake();

	bool ServiceCaptureBake();

	void UploadToGpu();
	void UploadVolumeToGpu();

	//////////////////////////////////////
	// Probe cache
	/////////////////////////////////////

	String GetProbeFilePath() const;

	bool SaveProbes();
	bool LoadProbes();

private:
	bool ComputeGridPlacement();
	bool GatherPlacementBoxes(ProbeBoxList& out);
	void PlaceGridProbes(const Vec3f& gmin, const Vec3f& size, const ProbeBoxList& boxes);

private:
	ProbeData mProbes[Limits::MaxIrradianceProbes] {};
	ProbeVolumeData mVolume {};
	bool mbInitialized = false;

	/// Capture bake state + resources (stage/staging built lazily).
	Vec3f mProbePositions[Limits::MaxIrradianceProbes] {};

	uint32 mNumProbesPending = 0;
	uint32 mCurrentProbe = 0;

	bool mbCapturePending = false;
	bool mbCaptureReady = false;
	bool mbCaptureBuilt = false;

	renderer::RenderStage mCaptureStage;
	renderer::RawGpuBuffer mCaptureStaging[scCaptureFaces];
	PerspectiveCamera mFaceCameras[scCaptureFaces];
};

extern ProbeManager* gProbeManager;

} // namespace fx
