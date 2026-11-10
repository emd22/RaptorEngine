#pragma once

#include <phonon/phonon.h>

#include <Audio/Acoustics.hpp>

namespace fx::Acoustics {

const IPLBakedDataIdentifier scReverbIdentifier = { IPL_BAKEDDATATYPE_REFLECTIONS, IPL_BAKEDDATAVARIATION_REVERB, {} };
const IPLBakedDataIdentifier scPathingIdentifier = { IPL_BAKEDDATATYPE_PATHING, IPL_BAKEDDATAVARIATION_DYNAMIC, {} };

IPLVector3 ToIPL(const FVec3 value);

IPLMaterial GetMaterial(const AcousticSurface& surface);

void IPLCALL OnSteamAudioLog(IPLLogLevel level, const char* message);

bool CreateContext(IPLContext& out_context);

bool CreateScene(IPLContext context, const AcousticGeometry& audio_geometry, IPLScene& out_scene,
				 IPLStaticMesh& out_mesh);

} // namespace fx::Acoustics
