#pragma once

#include <Audio/Acoustics.hpp>
#include <phonon/phonon.h>

namespace fx::Acoustics {

const IPLBakedDataIdentifier scReverbIdentifier = { IPL_BAKEDDATATYPE_REFLECTIONS, IPL_BAKEDDATAVARIATION_REVERB, {} };
const IPLBakedDataIdentifier scPathingIdentifier = { IPL_BAKEDDATATYPE_PATHING, IPL_BAKEDDATAVARIATION_DYNAMIC, {} };

IPLVector3 ToIPL(const Vec3f value);

IPLMaterial GetMaterial(const AcousticSurface& surface);

void IPLCALL OnSteamAudioLog(IPLLogLevel level, const char* message);

bool CreateScene(IPLContext context, const AcousticGeometry& audio_geometry, IPLScene& out_scene,
				 IPLStaticMesh& out_mesh);

} // namespace fx::Acoustics
