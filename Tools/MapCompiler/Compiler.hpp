/*
 * File:        Compiler.hpp
 * Author:      emd22
 * Created:     10/10/2026
 * Description: Runs every stage of the map compiler on one prototype file
 */

#pragma once

#include "Surfaces.hpp"

#include <functional>

namespace fx::mapc {

struct CompileOptions
{
	static constexpr uint32 scDefaultVerifyRays = 20000;

	String InputPath;
	/// Next to the prototype when empty
	String OutputPath;
	/// No OBJ is written when empty
	String ObjPath;

	SurfaceOptions Surface;

	bool bKeepOutside = false;

	bool bPlaceProbes = true;
	ProbePlacementSettings Probes;
	uint32 VerifyRays = 0;
};

enum class eSealState
{
	Unknown,
	Sealed,
	Leaked,
};

struct CompileResult
{
	bool bSucceeded = false;
	bool bVerifyFailed = false;

	eSealState Seal = eSealState::Unknown;
	SurfaceStats Stats;

	uint32 ProbeCount = 0;
	uint32 ProbeVolumeCount = 0;

	String OutputPath;
	String LeakPath;

	double Seconds = 0.0;
};

using LogFunction = std::function<void(const String&)>;

CompileResult CompileMap(const CompileOptions& options, const LogFunction& log);

} // namespace fx::mapc
