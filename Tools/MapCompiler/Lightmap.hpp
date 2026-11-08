/*
 * File:        Lightmap.hpp
 * Author:      emd22
 * Created:     12/10/2026
 * Description: Bakes how much of the sun reaches the compiled surfaces and the light probes
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

	bool bIndirect = true;
	uint32 IndirectRays = 256;
	uint32 IndirectBounces = 3;
	uint32 IndirectCacheMegabytes = 0;
	eDenoiseQuality IndirectDenoise = eDenoiseQuality::Medium;

	uint32 ProbeTexelRays = 4;

	/// Size of the cells that hold the bounced light of model surfaces, which have no lightmap texels
	double ModelCellSize = 0.25;

	bool bTransportStats = false;
	bool bLightTransport = true;
	/// Links that carry less than this fraction of a cluster's gather rays are dropped, even before coverage is met
	double TransportMinLinkFraction = 0.0005;

	FogShadowOptions FogShadow;

	String ImagePath;
};

/// Clusters are square blocks of 1, 2, 4 or 8 texels on a side, aligned to their chart
struct TransportLinkStats
{
	uint64 ReceiverCount = 0;
	uint64 UniqueLinkSum = 0;
	uint64 Link90Sum = 0;
	uint64 Link95Sum = 0;
	uint64 Link99Sum = 0;
	uint32 MaxLink95 = 0;
};

struct TransportStats
{
	static constexpr uint32 scLevelCount = 4;

	bool bMeasured = false;
	uint32 RaysPerTexel = 0;
	uint64 RayCount = 0;
	uint64 SkyRayCount = 0;
	uint64 DarkRayCount = 0;
	uint64 UnmappedRayCount = 0;
	uint64 ClusterCounts[scLevelCount] = {};
	double Seconds = 0.0;

	/// [receiver level][source level]
	TransportLinkStats Texels[scLevelCount][scLevelCount];

	uint32 ProbeRaysPerProbe = 0;
	TransportLinkStats Probes[scLevelCount];
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

	TransportStats Transport;

	uint32 TransportClusterCount = 0;
	uint32 TransportSampleCount = 0;
	uint64 TransportLinkCount = 0;
	double TransportSeconds = 0.0;

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
