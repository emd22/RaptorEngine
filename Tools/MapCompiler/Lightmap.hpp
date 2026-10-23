/*
 * File:        Lightmap.hpp
 * Author:      emd22
 * Created:     12/10/2026
 * Description: Bakes how much of the sun reaches the compiled surfaces and the light probes
 */

#pragma once

#include "Surfaces.hpp"

namespace fx::mapc {

enum class eDenoiseQuality : uint32
{
	Off,
	Low,
	Medium,
	High,
};

struct LightmapOptions
{
	bool bEnabled = true;

	double Density = 16.0;
	uint32 MaxSize = 4096;

	uint32 Samples = 4;
	double SunRadiusDegrees = 1.0;

	uint32 ProbeRays = 64;

	bool bIndirect = true;
	uint32 IndirectRays = 256;
	uint32 IndirectBounces = 3;
	uint32 IndirectCacheMegabytes = 0;
	eDenoiseQuality IndirectDenoise = eDenoiseQuality::Medium;

	uint32 ProbeTexelRays = 4;

	String ImagePath;
};

struct LightmapStats
{
	uint32 Width = 0;
	uint32 Height = 0;
	uint32 ChartCount = 0;
	uint32 OccluderCount = 0;
	uint32 OccluderTriangleCount = 0;

	double Density = 0.0;

	uint64 TexelCount = 0;
	uint64 RayCount = 0;

	uint32 ProbeCount = 0;
	uint32 LitProbeCount = 0;

	double LitFraction = 0.0;

	bool bHasIndirect = false;
	uint32 IndirectTriangleCount = 0;
	uint32 IndirectBlockerCount = 0;
	uint32 IndirectLightCount = 0;
	uint32 MissingAlbedoCount = 0;
	uint64 IndirectRayCount = 0;
	uint64 IndirectTexelCount = 0;
	uint64 IndirectCachedTexelCount = 0;
	double IndirectCacheMegabytes = 0.0;
	double MeanIndirect = 0.0;

	uint32 BakedProbeCount = 0;
	uint64 ProbeRayCount = 0;
	double MeanProbeIrradiance = 0.0;
};

class LightmapBaker
{
public:
	bool Bake(const PlaneSet& planes, const MapSource& source, const SurfaceBuilder& surfaces,
			  const PatchBuilder* patches, const LightmapOptions& options, CompiledMap& out_map);

public:
	LightmapStats Stats;
};

} // namespace fx::mapc
