/*
 * File:        Lightmap.hpp
 * Author:      emd22
 * Created:     12/10/2026
 * Description: Bakes how much of the sun reaches the compiled surfaces and the light probes
 */

#pragma once

#include "Surfaces.hpp"

namespace fx::mapc {

struct LightmapOptions
{
	bool bEnabled = true;

	double Density = 16.0;
	uint32 MaxSize = 4096;

	uint32 Samples = 4;
	double SunRadiusDegrees = 1.0;

	uint32 ProbeRays = 64;

	String ImagePath;
};

struct LightmapStats
{
	uint32 Width = 0;
	uint32 Height = 0;
	uint32 ChartCount = 0;
	uint32 OccluderCount = 0;

	double Density = 0.0;

	uint64 TexelCount = 0;
	uint64 RayCount = 0;

	uint32 ProbeCount = 0;
	uint32 LitProbeCount = 0;

	double LitFraction = 0.0;
};

class LightmapBaker
{
public:
	bool Bake(const PlaneSet& planes, const MapSource& source, const SurfaceBuilder& surfaces,
			  const LightmapOptions& options, CompiledMap& out_map);

public:
	LightmapStats Stats;
};

} // namespace fx::mapc
