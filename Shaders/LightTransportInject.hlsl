F_PROGRAM(FPT_COMPUTE)

#include "./Helper.hlsl"
#include "LightingCommon.hlsli"
#include "LightTransportCommon.hlsli"

F_CBuffer(FSLightBuffer, 0, 0)
{
	Light Lights[LIGHT_COUNT];
};

F_ShadowTexture2D(tShadowAtlas, 1, 0);

F_StructBuffer(bClusters, LightTransportCluster, 2, 0);
F_StructBuffer(bSamples, LightTransportSample, 3, 0);
F_RWStructBuffer(bState, float4, 4, 0);

F_FrameData(LightTransportConsts, Consts, 1);

/// Pushes the measuring point off its surface so it doesn't shadow itself
#define TRANSPORT_SAMPLE_OFFSET 0.05
#define TRANSPORT_SHADOW_BIAS 0.0

float SampleTransportShadow(Light light, float3 position_ws)
{
	const float4 atlas_rect = light.vShadowAtlasRect;

	if (atlas_rect.x <= 0.0) {
		return 1.0;
	}

	const float4 position_ls = mul(float4(position_ws, 1.0), light.LightCameraMatrix);
	const float3 position_ndc = position_ls.xyz / position_ls.w;

	const float2 shadow_uv = position_ndc.xy * 0.5 + 0.5;
	const float shadow_z = 1.0 - position_ndc.z;

	const bool behind = position_ls.w <= 0.0;
	const bool outside = or(behind, or(any(saturate(shadow_uv) != shadow_uv), shadow_z <= 0.0));

	const float2 texel = 1.0 / float2(SHADOW_ATLAS_WIDTH, SHADOW_ATLAS_HEIGHT);
	const float2 region_min = atlas_rect.zw + texel;
	const float2 region_max = atlas_rect.zw + atlas_rect.xy - texel;
	const float2 atlas_uv = shadow_uv * atlas_rect.xy + atlas_rect.zw;
	const float compare_z = shadow_z + TRANSPORT_SHADOW_BIAS;

	float lit = F_SampleCmpLevelZero(tShadowAtlas, clamp(atlas_uv + float2(texel.x, 0.0), region_min, region_max),
									 compare_z);
	lit += F_SampleCmpLevelZero(tShadowAtlas, clamp(atlas_uv - float2(texel.x, 0.0), region_min, region_max),
								compare_z);
	lit += F_SampleCmpLevelZero(tShadowAtlas, clamp(atlas_uv + float2(0.0, texel.y), region_min, region_max),
								compare_z);
	lit += F_SampleCmpLevelZero(tShadowAtlas, clamp(atlas_uv - float2(0.0, texel.y), region_min, region_max),
								compare_z);

	return select(outside, 1.0, lit * 0.25);
}

/// Direct light from the unbaked lights at every cluster, turned into the light that cluster sends back out
/// together with last frame's bounce
[numthreads(64, 1, 1)]
void main(uint3 thread : SV_DispatchThreadID)
{
	const uint cluster_index = thread.x;

	if (cluster_index >= Consts.uiClusterCount) {
		return;
	}

	const LightTransportCluster cluster = bClusters[cluster_index];

	float3 irradiance = float3(0.0, 0.0, 0.0);

	for (uint sample_index = 0; sample_index < cluster.uiSampleCount; sample_index++) {
		const LightTransportSample transport_sample = bSamples[cluster.uiFirstSample + sample_index];

		const float3 normal = transport_sample.vNormal.xyz;
		const float3 position = transport_sample.vPosition.xyz + normal * TRANSPORT_SAMPLE_OFFSET;

		for (uint light_slot = 0; light_slot < Consts.uiLightCount; light_slot++) {
			const Light light = Lights[Consts.uiFirstLight + light_slot];

			const bool is_directional = light.uiLightType == FX_LIGHT_TYPE_DIRECTIONAL;
			const bool is_spot = light.uiLightType == FX_LIGHT_TYPE_SPOT;

			const float3 to_light = light.vLightPosition - position;
			const float dist_sq = dot(to_light, to_light);
			const float3 L = to_light * rsqrt(max(dist_sq, 1e-8));

			float attenuation = light.fIntensity * AttenuationSmooth(dist_sq, light.fInvRadiusSq);
			attenuation *= select(is_spot, AttenuationSpot(L, light), 1.0);
			attenuation *= saturate(dot(normal, L));

			// The sun is baked, and directional lights would need the cascades
			attenuation = select(is_directional, 0.0, attenuation);

			const float visibility = SampleTransportShadow(light, position);

			irradiance += light.vLightColor * (attenuation * visibility);
		}
	}

	// Stored as irradiance / pi, like the baked lightmap
	const float sample_scale = 1.0 / (max(float(cluster.uiSampleCount), 1.0) * FX_MATH_PI);
	const float3 previous = bState[Consts.uiReadOffset + cluster_index].rgb;

	bState[cluster_index] = float4(cluster.vAlbedo * (irradiance * sample_scale + previous), 0.0);
}
