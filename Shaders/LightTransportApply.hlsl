F_PROGRAM(FPT_COMPUTE)

#include "./Helper.hlsl"
#include "LightTransportCommon.hlsli"

F_Texture2D(tBakedIrradiance, 0, 0);

F_StructBuffer(bCharts, LightTransportChart, 1, 0);
F_StructBuffer(bClusters, LightTransportCluster, 2, 0);
F_StructBuffer(bState, float4, 3, 0);
F_StructBuffer(bTexelCharts, uint, 4, 0);

[[vk::image_format("rgba16f")]] [[vk::binding(5, 0)]] RWTexture2D<float4> tRelitIrradiance;

F_FrameData(LightTransportConsts, Consts, 1);

uint GetTexelChart(uint2 texel)
{
	const uint index = texel.x + texel.y * Consts.vLightmapSize.x;
	const uint packed = bTexelCharts[index / 2];

	return select((index & 1u) != 0u, packed >> 16, packed & 0xFFFFu);
}

/// The baked irradiance plus this frame's bounce, blended between the four nearest clusters of the texel's chart
[numthreads(8, 8, 1)]
void main(uint3 thread : SV_DispatchThreadID)
{
	const uint2 texel = thread.xy;

	if (any(texel >= Consts.vLightmapSize)) {
		return;
	}

	const float4 baked = F_SampleLoad(tBakedIrradiance, int3(texel, 0));
	const uint chart_index = GetTexelChart(texel);

	if (or(chart_index == LIGHT_TRANSPORT_NO_CHART, !HAS_FLAG(Consts.uiFlags, LIGHT_TRANSPORT_FLAG_SHOW))) {
		tRelitIrradiance[texel] = baked;
		return;
	}

	const LightTransportChart chart = bCharts[chart_index];

	const float2 local = float2(texel - chart.vMin) + 0.5;
	const float2 block = local / float(Consts.uiClusterSize) - 0.5;
	const float2 first_block = floor(block);
	const float2 blend = block - first_block;

	const int2 max_block = int2(chart.vBlocks) - int2(1, 1);

	float3 sum = float3(0.0, 0.0, 0.0);
	float weight_sum = 0.0;

	[unroll]
	for (uint corner = 0; corner < 4; corner++) {
		const int2 offset = int2(corner & 1u, corner >> 1);
		const int2 corner_block = clamp(int2(first_block) + offset, int2(0, 0), max_block);

		const uint cluster_index = chart.uiFirstCluster + uint(corner_block.y) * chart.vBlocks.x + uint(corner_block.x);

		const float2 corner_blend = select(offset != int2(0, 0), blend, 1.0 - blend);
		const bool has_texels = bClusters[cluster_index].uiSampleCount > 0;
		const float weight = select(has_texels, corner_blend.x * corner_blend.y, 0.0);

		sum += bState[Consts.uiWriteOffset + cluster_index].rgb * weight;
		weight_sum += weight;
	}

	const float3 bounce = sum / max(weight_sum, 1e-4);

	tRelitIrradiance[texel] = float4(baked.rgb + bounce, baked.a);
}
