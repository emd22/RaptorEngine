#include "./Helper.hlsl"

F_PROGRAM(FPT_VERTEX)

struct VSInput
{
	int iVertexIndex : SV_VertexID;
};

struct VSOutput
{
	float4 vPosition : SV_POSITION;
};

VSOutput main(VSInput input)
{
	VSOutput output;

	const float2 uv = float2((input.iVertexIndex << 1) & 2, input.iVertexIndex & 2);
	output.vPosition = float4(uv * 2.0 - 1.0, 0.0, 1.0);

	return output;
}

F_PROGRAM(FPT_PIXEL)

#include "LightingCommon.hlsli"
#include "ProbeClipmapForward.hlsli"
#include "ProbeClipmapResolve.hlsli"

struct FSInput
{
	float4 vPosition : SV_POSITION;
};

struct FSOutput
{
	/// Irradiance / pi along the normal, distance from the eye in A
	float4 vDiffuse : SV_TARGET0;
	/// Irradiance / pi along the reflection, see GIResolvePackSpecular
	uint uiSpecular : SV_TARGET1;
	/// The normal the gather used, octahedral
	float2 vNormal : SV_TARGET2;
};

struct ProbeClipmapResolveConsts
{
	float4x4 mInvViewProjection;
	float4 vEyePosition;
	/// Full resolution width and height, flags (GI_RESOLVE_FLAG_*)
	uint4 vInfo;
	/// Where the eye was last frame, which the history's distances are from
	float4 vPrevEyePosition;
	/// Screen probe tile size
	uint4 vScreenProbes;
	/// How much of each frame's light goes into the history (the rest is the history), the least it takes after a
	/// disocclusion ramp
	float4 vTemporal;
};

#define GI_RESOLVE_FLAG_SCREEN_PROBES 0x01
#define GI_RESOLVE_FLAG_HISTORY 0x02

/// A history texel stands for the pixel when its distance from last frame's eye is within this share of the expected
#define GI_HISTORY_DEPTH_SHARE 0.03
#define GI_HISTORY_MIN_NORMAL 0.9

F_Texture2D(tDepth, 0, 0);
F_Texture2D(tNormal, 1, 0);
F_Texture2D(tIrradiance, 2, 0);
F_Texture2D(tMoments, 3, 0);
F_StructBuffer(bProbes, uint4, 4, 0);
F_StructBuffer(bClipmapForward, ClipmapForwardData, 5, 0);
/// Prepass motion: last frame's uv minus this frame's
F_Texture2D(tMotion, 6, 0);
/// Last frame's output: diffuse (A eye distance) and the normal
F_Texture2D(tHistoryDiffuse, 7, 0);
F_Texture2D(tHistoryNormal, 8, 0);
F_StructBuffer(bScreenProbes, float4, 9, 0);

F_FrameData(ProbeClipmapResolveConsts, Consts, 1);

#define CLIPMAP_SCENE bClipmapForward[0].Clipmap
#include "ProbeClipmapSample.hlsli"
#include "ScreenProbeSample.hlsli"

/// The viewmodel's depth is squashed into the top of the range, see the viewmodel projection
#define VIEWMODEL_STORED_DEPTH 0.99

float3 ResolveWorldPosition(int2 pixel)
{
	const int2 clamped = clamp(pixel, int2(0, 0), int2(Consts.vInfo.xy) - 1);
	const float stored_depth = F_SampleLoad(tDepth, int3(clamped, 0)).r;
	const float2 uv = (float2(clamped) + 0.5) / float2(Consts.vInfo.xy);
	const float4 world = mul(float4(uv * 2.0 - 1.0, 1.0 - stored_depth, 1.0), Consts.mInvViewProjection);

	return world.xyz / world.w;
}

FSOutput main(FSInput input)
{
	FSOutput output;

	const uint2 pixel = min(uint2(input.vPosition.xy) * 2u, Consts.vInfo.xy - 1u);
	const float stored_depth = F_SampleLoad(tDepth, int3(pixel, 0)).r;
	const float4 normal_roughness = F_SampleLoad(tNormal, int3(pixel, 0));

	const bool has_surface = and(and(stored_depth > 0.0, stored_depth < VIEWMODEL_STORED_DEPTH),
								 dot(normal_roughness.xyz, normal_roughness.xyz) > 0.5);

	const float2 uv = (float2(pixel) + 0.5) / float2(Consts.vInfo.xy);
	const float4 ndc = float4(uv * 2.0 - 1.0, 1.0 - stored_depth, 1.0);
	const float4 world = mul(ndc, Consts.mInvViewProjection);
	const float3 position = world.xyz / world.w;

	const float3 N = normalize(normal_roughness.xyz);
	const float3 to_eye = Consts.vEyePosition.xyz - position;
	const float distance = length(to_eye);
	const float3 V = to_eye / max(distance, 1e-6);

	const float specular_alpha = normal_roughness.w * normal_roughness.w;
	const float3 R = GetSpecularDominantDir(N, reflect(-V, N), specular_alpha);

	output.vDiffuse = float4(0.0, 0.0, 0.0, GI_RESOLVE_NO_SURFACE);
	output.uiSpecular = 0;
	output.vNormal = GIResolveEncodeNormal(N);

	// Sky and viewmodel pixels gather nothing, and the forward pass gathers the viewmodel's itself
	if (has_surface) {
		// The probe weights and the bias go by the surface's own normal, the light is read along the normal mapped one
		const int2 at = int2(pixel);
		const float3 geometric_normal =
			GI_RESOLVE_GEOMETRIC_NORMAL(ResolveWorldPosition, position, at, V, distance * 0.05, N);

		const ClipmapGatherResult clipmap = ClipmapGather(position, geometric_normal, V, N, R);

		const bool in_clipmap = clipmap.uiLevel < CLIPMAP_LEVELS;
		const float3 fallback = bClipmapForward[0].vFallback.rgb;

		float3 diffuse = select(in_clipmap, clipmap.vDiffuse, fallback);
		diffuse = select(GIIsFinite(diffuse), diffuse, float3(0.0, 0.0, 0.0));

		if (HAS_FLAG(Consts.vInfo.z, GI_RESOLVE_FLAG_SCREEN_PROBES)) {
			float screen_weight;
			const float3 screen = ScreenProbeGather(position, distance, geometric_normal, N, pixel, Consts.vInfo.xy,
													Consts.vScreenProbes.x, screen_weight);
			diffuse = select(GIIsFinite(screen), lerp(diffuse, screen, ScreenProbeShare(screen_weight)), diffuse);
		}

		// Last frame's light at the same surface, read from where the pixel was. Each history texel only counts if it
		// was the same surface: as far from last frame's eye as this point is, facing the same way.
		if (HAS_FLAG(Consts.vInfo.z, GI_RESOLVE_FLAG_HISTORY)) {
			uint history_width;
			uint history_height;
			F_TextureName(tHistoryDiffuse).GetDimensions(history_width, history_height);

			const float2 previous_uv = uv + F_SampleLoad(tMotion, int3(pixel, 0)).xy;
			// Texel i holds full resolution pixel 2i
			const float2 history_position = previous_uv * float2(Consts.vInfo.xy) * 0.5 - 0.25;
			const int2 history_base = int2(floor(history_position));
			const float2 history_frac = history_position - float2(history_base);
			const float expected = length(position - Consts.vPrevEyePosition.xyz);

			float3 history = float3(0.0, 0.0, 0.0);
			float history_weight = 0.0;

			[unroll]
			for (uint tap = 0; tap < 4; tap++) {
				const int2 offset = int2(tap & 1, tap >> 1);
				const int2 coord = history_base + offset;
				const bool in_bounds = and(all(coord >= 0), all(coord < int2(history_width, history_height)));
				const int3 load = int3(clamp(coord, int2(0, 0), int2(history_width, history_height) - 1), 0);

				const float4 texel = F_SampleLoad(tHistoryDiffuse, load);
				const float3 texel_normal = GIResolveDecodeNormal(F_SampleLoad(tHistoryNormal, load).xy);

				const float2 axis = lerp(1.0 - history_frac, history_frac, float2(offset));
				const bool same_depth = abs(texel.a - expected) < expected * GI_HISTORY_DEPTH_SHARE;
				const bool same_facing = dot(texel_normal, N) > GI_HISTORY_MIN_NORMAL;
				const bool is_usable = and(and(in_bounds, texel.a > 0.0),
										   and(and(same_depth, same_facing), GIIsFinite(texel.rgb)));
				const float weight = select(is_usable, axis.x * axis.y, 0.0);

				// Selected, not multiplied away: 0 * NaN is NaN, and one NaN texel would spread to every pixel
				// that reads it, frame after frame
				history += select(weight > 0.0, texel.rgb * weight, float3(0.0, 0.0, 0.0));
				history_weight += weight;
			}

			// Mostly last frame's where it holds the surface, all this frame's where it doesn't. A partly held
			// surface (an edge coming into view) leans on the new light.
			const float held = saturate(history_weight * 2.0 - 0.5);
			const float blend = lerp(1.0, Consts.vTemporal.x, held);
			diffuse = select(history_weight > 1e-3, lerp(history / max(history_weight, 1e-6), diffuse, blend), diffuse);
		}

		output.vDiffuse = float4(diffuse, distance);
		output.uiSpecular = GIResolvePackSpecular(select(in_clipmap, clipmap.vSpecular, fallback),
												   or(in_clipmap, any(fallback > 0.0)));
	}

	return output;
}
