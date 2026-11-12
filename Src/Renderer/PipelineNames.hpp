#pragma once

#include <Core/Types.hpp>

namespace fx::renderer {

/**
 * @brief The pipelines that are built once at startup and drawn with by name. Pipelines that come in variants
 * (geometry, prepass and shadow) are made from a PipelineDesc when they are first needed instead
 */
enum class ePipelineName : uint16
{
	DebugLayer,
	DebugSolid,
	DebugOverlay,

	/**
	 * @brief Forward+ compute pass that culls lights into screen space tiles, each ending at the prepass depth
	 */
	LightCulling,

	/**
	 * @brief LightCulling without depth bounds, for probe captures, which have no prepass
	 */
	LightCullingCapture,

	TextRendering,
	ImageRendering,
	Particles,
	Composition,
	CompositionAgx,

	SSAO,
	SSAODenoise,
	SSAOBlur,

	FogFroxelInject,
	FogFroxelIntegrate,
	FogFroxelApply,
	SDFPageUpdate,
	SDFPageDebug,
	/// The probe trace with the bounce (GI_BOUNCE) and without it, then the blend
	ProbeClipmapTrace,
	ProbeClipmapTraceNoBounce,
	ProbeClipmapBlend,
	ProbeClipmapDebug,
	/// The GI resolve with and without the screen probes (GI_SCREEN_PROBES) and the history (GI_HISTORY), writing
	/// GIResolvePasses[0], then the same writing GIResolvePasses[1]. See ProbeClipmapResolvePipeline.
	ProbeClipmapResolve,
	ProbeClipmapResolveScreenProbes,
	ProbeClipmapResolveHistory,
	ProbeClipmapResolveScreenProbesHistory,
	ProbeClipmapResolveB,
	ProbeClipmapResolveScreenProbesB,
	ProbeClipmapResolveHistoryB,
	ProbeClipmapResolveScreenProbesHistoryB,
	/// The screen probe trace through the SDF pages alone, and first through last frame's lit scene
	/// (SCREEN_PROBE_SCREEN_TRACE)
	ScreenProbeTrace,
	ScreenProbeTraceScreen,
	ScreenProbeFilter,
	ScreenProbeLitHistory,

	NumPipelines
};

constexpr uint32 scNumPipelines = static_cast<uint32>(ePipelineName::NumPipelines);

/// The GI resolve pipeline that reads the screen probes and the history as given and writes resolve stage `write`
constexpr ePipelineName ProbeClipmapResolvePipeline(bool screen_probes, bool history, uint32 write)
{
	uint32 index = static_cast<uint32>(ePipelineName::ProbeClipmapResolve) + write * 4;

	if (screen_probes) {
		index += 1;
	}

	if (history) {
		index += 2;
	}

	return static_cast<ePipelineName>(index);
}


struct PipelineNameInfo
{
	const char* pcName;
};

const PipelineNameInfo& GetPipelineNameInfo(const ePipelineName name);


namespace PipelineNameUtil {

FX_FORCE_INLINE const char* GetName(const ePipelineName id) { return GetPipelineNameInfo(id).pcName; }


} // namespace PipelineNameUtil


} // namespace fx::renderer
