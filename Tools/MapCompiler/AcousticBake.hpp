#pragma once

#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <functional>

namespace fx {

class CompiledMap;

namespace mapc {

struct AcousticOptions
{
	bool bEnabled = true;
	bool bPathing = true;

	double ProbeSpacing = 3.0;
	double ProbeHeight = 1.5;

	uint32 ReverbRays = 16384;
	uint32 ReverbBounces = 64;
};

struct AcousticStats
{
	bool bBaked = false;
	bool bPathing = false;

	uint32 ProbeCount = 0;
	Vec3f ProbeMin = Vec3f::sZero;
	Vec3f ProbeMax = Vec3f::sZero;

	size_t DataSize = 0;

	double ReflectionSeconds = 0.0;
	double PathingSeconds = 0.0;

	String Error;
};

bool BakeAcoustics(const AcousticOptions& options, CompiledMap& map, AcousticStats& out_stats,
				   const std::function<void(const String&)>& log);

} // namespace mapc
} // namespace fx
