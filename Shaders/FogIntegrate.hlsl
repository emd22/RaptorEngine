F_PROGRAM(FPT_COMPUTE)

#include "./Helper.hlsl"
#include "LightingCommon.hlsli"
#include "FogConsts.hlsli"

F_Texture3D(tFroxelLighting, 0, 0);

[[vk::image_format("rgba16f")]] F_RWTexture3D(tFroxelIntegrated, float4, 1, 0);

F_FrameData(FogConsts, Consts, 1);

#include "FogMedium.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 column : SV_DispatchThreadID)
{
	if (any(column.xy >= uint2(FROXEL_GRID_X, FROXEL_GRID_Y))) {
		return;
	}

	const float2 uv = (float2(column.xy) + 0.5) / float2(FROXEL_GRID_X, FROXEL_GRID_Y);
	const float3 ray_unit_depth = FogScreenRay(uv);
	const float ray_length = length(ray_unit_depth);

	const FogRay ray = MakeEyeFogRay(ray_unit_depth / ray_length);

	const float3 scale = Consts.vAlbedoAndFalloff.rgb * Consts.fPreExposure;

	float3 inscatter = float3(0.0, 0.0, 0.0);
	float start_transmittance = 1.0;

	for (uint slice = 0; slice < FROXEL_GRID_Z; slice++) {
		const float end_distance = FroxelSliceToDepth(float(slice + 1)) * ray_length;
		const float end_transmittance = FogTransmittance(ray, end_distance);

		const float3 light = F_SampleLoad(tFroxelLighting, int4(column.xy, slice, 0)).rgb;

		inscatter += light * (start_transmittance - end_transmittance);
		start_transmittance = end_transmittance;

		tFroxelIntegrated[uint3(column.xy, slice)] = float4(inscatter * scale, end_transmittance);
	}
}
