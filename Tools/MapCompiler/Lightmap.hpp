/*
 * File:        Lightmap.hpp
 * Author:      emd22
 * Created:     12/10/2026
 * Description: Bakes how much of the sun reaches the compiled surfaces, and the reference bounced light
 */

#pragma once

#include "FogShadow.hpp"
#include "Surfaces.hpp"

namespace fx::mapc {

class ModelBuilder;

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

	/// The runtime GI probes light the map. The bounced light is only baked as a reference to compare them against,
	/// into lightmap irradiance and SH probes the engine does not load.
	bool bIndirect = false;
	uint32 IndirectRays = 256;
	uint32 IndirectBounces = 3;
	uint32 IndirectCacheMegabytes = 0;
	eDenoiseQuality IndirectDenoise = eDenoiseQuality::Medium;

	uint32 ProbeTexelRays = 4;

	/// Size of the cells that hold the bounced light of model surfaces, which have no lightmap texels
	double ModelCellSize = 0.25;

	FogShadowOptions FogShadow;

	String ImagePath;
};

struct LightmapStats
{
	uint32 Width = 0;
	uint32 Height = 0;
	uint32 ChartCount = 0;
	uint32 OccluderCount = 0;
	uint32 OccluderTriangleCount = 0;
	uint32 OccluderModelTriangleCount = 0;

	double Density = 0.0;

	uint64 TexelCount = 0;
	uint64 RayCount = 0;

	uint32 ProbeCount = 0;
	uint32 LitProbeCount = 0;

	double LitFraction = 0.0;

	bool bHasIndirect = false;
	uint32 IndirectTriangleCount = 0;
	uint32 IndirectBlockerCount = 0;
	uint32 IndirectModelTriangleCount = 0;
	uint32 IndirectModelCellCount = 0;
	uint32 IndirectWaterVolumeCount = 0;
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

	bool bHasFogShadow = false;
	FogShadowStats FogShadow;
};

class LightmapBaker
{
public:
	bool Bake(const PlaneSet& planes, const MapSource& source, const SurfaceBuilder& surfaces,
			  const PatchBuilder* patches, const ModelBuilder* models, const LightmapOptions& options,
			  CompiledMap& out_map);

public:
	LightmapStats Stats;
};

} // namespace fx::mapc
