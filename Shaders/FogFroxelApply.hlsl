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

F_FrameData(FogConsts, Consts, 1);

#include "FogMedium.hlsli"

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

	const float3 grid_inscatter = F_SampleLevel(tFroxelIntegrated, float3(uv, integrated_w), 0).rgb * saturate(slice);

	const float grid_transmittance = FogTransmittance(ray, grid_depth * ray_length);
	const float transmittance = FogTransmittance(ray, view_depth * ray_length);

	const float tail_w = (float(FROXEL_GRID_Z) + 0.5) / float(FROXEL_GRID_Z + 1);
	const float3 tail_light = F_SampleLevel(tFroxelLighting, float3(uv, tail_w), 0).rgb;
	const float3 tail_scale = Consts.vAlbedoAndFalloff.rgb * Consts.fPreExposure;

	const float3 tail = tail_light * tail_scale * (grid_transmittance - transmittance);

	output.vFog = float4(grid_inscatter + tail, transmittance);

	return output;
}
