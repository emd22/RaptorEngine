#include "./Helper.hlsl"

//////////////////////////////////
// Vertex shader
//////////////////////////////////

F_PROGRAM(FPT_VERTEX)

struct VSInput
{
	int iVertexIndex : SV_VertexID;
};

struct VSOutput
{
	float4 vPosition : SV_POSITION;
	float2 vUV : TEXCOORD0;
};

VSOutput main(VSInput input)
{
	VSOutput output;

	float2 out_uv = float2((input.iVertexIndex << 1) & 2, input.iVertexIndex & 2);

	output.vUV = out_uv;
	output.vPosition = float4(out_uv * 2.0 - 1.0, 0.0, 1.0);

	return output;
}

//////////////////////////////////
// Fragment shader
//////////////////////////////////

F_PROGRAM(FPT_PIXEL)

#include "LightingCommon.hlsli"
#include "FogConsts.hlsli"

struct FSInput
{
	float4 vPosition : SV_POSITION;
	float2 vUV : TEXCOORD0;
};

struct FSOutput
{
	float4 vFog : SV_TARGET0;
};

F_Texture2D(tDepth, 0, 0);
F_Texture3D(tFroxelIntegrated, 1, 0);
F_Texture3D(tFroxelLighting, 2, 0);
F_Texture3D(tFroxelSurface, 3, 0);

F_FrameData(FogConsts, Consts, 1);

#include "FogMedium.hlsli"

#define FROXEL_UPSAMPLE_TOLERANCE 0.25
#define FROXEL_UPSAMPLE_RELATIVE_TOLERANCE 0.05
#define FROXEL_UPSAMPLE_REJECTED_WEIGHT 1e-4

struct FroxelUpsample
{
	float3 Integrated;
	float3 Tail;
	float Weight;
	float AcceptedWeight;
};

void AddFroxelColumn(inout FroxelUpsample upsample, int2 column, float base_weight, float view_depth,
					 float integrated_w)
{
	const int2 column_limit = int2(FROXEL_GRID_X, FROXEL_GRID_Y) - int2(1, 1);
	column = clamp(column, int2(0, 0), column_limit);

	const float surface_limit = F_SampleLoad(tFroxelSurface, int4(column, 0, 0)).g;
	const float tolerance = FROXEL_UPSAMPLE_TOLERANCE + (FROXEL_UPSAMPLE_RELATIVE_TOLERANCE * surface_limit);
	const float acceptance = saturate((surface_limit + (2.0 * tolerance) - view_depth) / tolerance);

	const float weight = base_weight * max(acceptance, FROXEL_UPSAMPLE_REJECTED_WEIGHT);

	const float2 column_uv = (float2(column) + 0.5) / float2(FROXEL_GRID_X, FROXEL_GRID_Y);

	upsample.Integrated += F_SampleLevel(tFroxelIntegrated, float3(column_uv, integrated_w), 0).rgb * weight;
	upsample.Tail += F_SampleLoad(tFroxelLighting, int4(column, FROXEL_GRID_Z, 0)).rgb * weight;
	upsample.Weight += weight;
	upsample.AcceptedWeight += base_weight * acceptance;
}

FSOutput main(FSInput input)
{
	FSOutput output;

	uint depth_width;
	uint depth_height;
	F_TextureName(tDepth).GetDimensions(depth_width, depth_height);

	const int2 pixel = int2(input.vPosition.xy);
	const float view_depth = LinearViewDepth(F_SampleLoad(tDepth, int3(pixel, 0)).r);

	const float2 uv = (float2(pixel) + 0.5) / float2(depth_width, depth_height);
	const float3 ray_unit_depth = FogScreenRay(uv);
	const float ray_length = length(ray_unit_depth);

	const FogRay ray = MakeEyeFogRay(ray_unit_depth / ray_length);

	const float grid_depth = min(view_depth, Consts.fMarchDistance);
	const float slice = FroxelDepthToSlice(grid_depth);
	const float integrated_w = (slice - 0.5) / float(FROXEL_GRID_Z);

	const float2 grid_size = float2(FROXEL_GRID_X, FROXEL_GRID_Y);
	const float2 grid_position = (uv * grid_size) - 0.5;
	const float2 base_column = floor(grid_position);
	const float2 column_fraction = grid_position - base_column;
	FroxelUpsample upsample = (FroxelUpsample)0;

	[unroll]
	for (uint inner = 0; inner < 4; inner++) {
		const int2 offset = int2(inner & 1, inner >> 1);
		const float2 bilinear = select(offset == int2(1, 1), column_fraction, 1.0 - column_fraction);

		AddFroxelColumn(upsample, int2(base_column) + offset, bilinear.x * bilinear.y, view_depth, integrated_w);
	}

	[branch]
	if (upsample.AcceptedWeight <= 0.0) {
		[unroll]
		for (uint outer = 0; outer < 16; outer++) {
			const int2 offset = int2(outer & 3, outer >> 2) - int2(1, 1);
			const bool is_inner = all(and(offset >= int2(0, 0), offset <= int2(1, 1)));

			AddFroxelColumn(upsample, int2(base_column) + offset, select(is_inner, 0.0, 1.0), view_depth,
							integrated_w);
		}
	}

	const float3 integrated_sum = upsample.Integrated;
	const float3 tail_sum = upsample.Tail;
	const float weight_sum = upsample.Weight;

	const float inv_weight = 1.0 / max(weight_sum, 1e-8);

	const float3 grid_inscatter = integrated_sum * inv_weight * saturate(slice);

	const float grid_transmittance = FogTransmittance(ray, grid_depth * ray_length);
	const float transmittance = FogTransmittance(ray, view_depth * ray_length);

	const float3 tail_light = tail_sum * inv_weight;
	const float3 tail_scale = Consts.vAlbedoAndFalloff.rgb * Consts.fPreExposure;

	const float3 tail = tail_light * tail_scale * (grid_transmittance - transmittance);

	output.vFog = float4(grid_inscatter + tail, transmittance);

	return output;
}
