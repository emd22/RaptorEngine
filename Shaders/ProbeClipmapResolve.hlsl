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
	/// Full resolution width and height
	uint4 vInfo;
};

F_Texture2D(tDepth, 0, 0);
F_Texture2D(tNormal, 1, 0);
F_Texture2D(tIrradiance, 2, 0);
F_Texture2D(tMoments, 3, 0);
F_StructBuffer(bProbes, uint4, 4, 0);
F_StructBuffer(bClipmapForward, ClipmapForwardData, 5, 0);

F_FrameData(ProbeClipmapResolveConsts, Consts, 1);

#define CLIPMAP_SCENE bClipmapForward[0].Clipmap
#include "ProbeClipmapSample.hlsli"

/// The viewmodel's depth is squashed into the top of the range, see the viewmodel projection
#define VIEWMODEL_STORED_DEPTH 0.99

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
		const ClipmapGatherResult clipmap = ClipmapGather(position, N, V, N, R);

		const bool in_clipmap = clipmap.uiLevel < CLIPMAP_LEVELS;
		const float3 fallback = bClipmapForward[0].vFallback.rgb;

		output.vDiffuse = float4(select(in_clipmap, clipmap.vDiffuse, fallback), distance);
		output.uiSpecular = GIResolvePackSpecular(select(in_clipmap, clipmap.vSpecular, fallback),
												   or(in_clipmap, any(fallback > 0.0)));
	}

	return output;
}
