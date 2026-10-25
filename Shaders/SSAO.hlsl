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

struct FSInput
{
	float2 vUV : TEXCOORD0;
};

struct FSOutput
{
	float2 vOut : SV_TARGET0;
};

F_Texture2D(tDepth, 0, 0);
F_Texture2D(tNormal, 1, 0);

struct SSAOConsts
{
	float4x4 InvProjection;
	float4x4 Projection;
	float4x4 View;
	float2 ScreenSize;
	float EffectRadius;
	float FalloffRange;
	float SampleDistributionPower;
	float ThinOccluderCompensation;
	float FinalValuePower;
};

F_FrameData(SSAOConsts, Consts, 1);

// Follows Intel's XeGTAO (Jimenez et al., "Practical Real-Time Strategies for Accurate Indirect Occlusion") at its
// High preset. The depth MIP chain is left out, every tap reads the full resolution depth target.
#define GTAO_SLICE_COUNT 3
#define GTAO_STEP_COUNT 3
#define GTAO_PIXEL_TOO_CLOSE_THRESHOLD 1.3
#define GTAO_OCCLUSION_TERM_SCALE 1.5
#define GTAO_HILBERT_WIDTH 64
#define PI 3.14159265
#define HALF_PI 1.57079633
#define VIEWMODEL_DEPTH_SCALE 0.01

float3 ReconstructViewPos(float2 uv, float depth)
{
	depth = select(depth < VIEWMODEL_DEPTH_SCALE, depth / VIEWMODEL_DEPTH_SCALE, depth);

	float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);

	float4 view_space = mul(ndc, Consts.InvProjection);

	return view_space.xyz / view_space.w;
}

float SampleRawDepth(float2 uv)
{
	const float2 depth_size = Consts.ScreenSize * SSAO_SIZE_DIVISOR;

	return F_SampleLevel(tDepth, uv + (0.25 / depth_size), 0).r;
}

float3 SampleViewPos(float2 uv)
{
	// The samplers repeat, so an unclamped tap past the screen edge would read the opposite side of the screen
	const float2 half_pixel = 0.5 / Consts.ScreenSize;
	const float2 clamped_uv = clamp(uv, half_pixel, 1.0 - half_pixel);

	return ReconstructViewPos(clamped_uv, 1.0 - SampleRawDepth(clamped_uv));
}

float FastACos(float x)
{
	const float ax = min(abs(x), 1.0);
	const float result = ((-0.156583 * ax) + HALF_PI) * sqrt(1.0 - ax);

	return select(x >= 0.0, result, PI - result);
}

uint HilbertIndex(uint2 position)
{
	uint index = 0;

	[unroll]
	for (uint level = GTAO_HILBERT_WIDTH / 2; level > 0; level /= 2)
	{
		const uint region_x = uint((position.x & level) > 0);
		const uint region_y = uint((position.y & level) > 0);

		index += level * level * ((3 * region_x) ^ region_y);

		const bool2 flip = (region_x == 1).xx;
		const bool2 swap = (region_y == 0).xx;

		const uint2 flipped = select(flip, (GTAO_HILBERT_WIDTH - 1) - position, position);
		position = select(swap, flipped.yx, position);
	}

	return index;
}

float2 SpatialNoise(uint2 pixel)
{
	const uint index = HilbertIndex(pixel % GTAO_HILBERT_WIDTH);

	return frac(0.5 + (float(index) * float2(0.75487766624669276005, 0.5698402909980532659114)));
}

float4 CalculateEdges(float center_z, float left_z, float right_z, float top_z, float bottom_z)
{
	float4 edges = float4(left_z, right_z, top_z, bottom_z) - center_z;

	const float slope_lr = (edges.y - edges.x) * 0.5;
	const float slope_tb = (edges.w - edges.z) * 0.5;
	const float4 slope_adjusted = edges + float4(slope_lr, -slope_lr, slope_tb, -slope_tb);

	edges = min(abs(edges), abs(slope_adjusted));

	return saturate(1.25 - (edges / (center_z * 0.011)));
}

float PackEdges(float4 edges)
{
	edges = round(saturate(edges) * 2.9);

	return dot(edges, float4(64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0));
}

float2 ComputeGTAO(float2 uv)
{
	const float2 pixel_size = 1.0 / Consts.ScreenSize;
	const float unoccluded = 1.0 / GTAO_OCCLUSION_TERM_SCALE;

	const float raw_depth = SampleRawDepth(uv);

	// Skip the skybox. The viewport runs minDepth 1 -> maxDepth 0 and the depth target clears to 0, so the far
	// plane (and anything the prepass never drew) reads as 0.0, not 1.0.
	if (raw_depth <= 0.0)
	{
		return float2(unoccluded, 0.0);
	}

	const float3 center_position = ReconstructViewPos(uv, 1.0 - raw_depth);

	const float edges = PackEdges(CalculateEdges(center_position.z,
												 SampleViewPos(uv + (float2(-1.0, 0.0) * pixel_size)).z,
												 SampleViewPos(uv + (float2(1.0, 0.0) * pixel_size)).z,
												 SampleViewPos(uv + (float2(0.0, -1.0) * pixel_size)).z,
												 SampleViewPos(uv + (float2(0.0, 1.0) * pixel_size)).z));

	const float3 world_normal = F_Sample(tNormal, uv).xyz;

	// DepthNormal.hlsl leaves unlit materials at a zero normal. normalize() would turn that into a NaN that
	// SSAOBlur would then spread over the neighbouring geometry.
	if (dot(world_normal, world_normal) < 1e-8)
	{
		return float2(unoccluded, edges);
	}

	const float3 position = center_position * 0.99998;
	const float3 view_vector = normalize(-position);
	const float3 normal = normalize(mul(world_normal, (float3x3)Consts.View));

	const float effect_radius = Consts.EffectRadius;
	const float falloff_range = Consts.FalloffRange * effect_radius;
	const float falloff_from = effect_radius * (1.0 - Consts.FalloffRange);
	const float falloff_mul = -1.0 / falloff_range;
	const float falloff_add = (falloff_from / falloff_range) + 1.0;

	// The view space offset that one pixel covers at this depth. Signed, so a projection that flips an axis still
	// marches the screen in the direction the slice points in view space.
	const float2 projection_scale = float2(mul(float4(1.0, 0.0, 0.0, 0.0), Consts.Projection).x,
										   mul(float4(0.0, 1.0, 0.0, 0.0), Consts.Projection).y) * 0.5;
	const float2 pixels_per_unit = (projection_scale * Consts.ScreenSize) / position.z;
	const float screenspace_radius = effect_radius * abs(pixels_per_unit.x);

	const float2 noise = SpatialNoise(uint2(uv * Consts.ScreenSize));
	const float min_s = GTAO_PIXEL_TOO_CLOSE_THRESHOLD / screenspace_radius;

	float visibility = saturate((10.0 - screenspace_radius) / 100.0) * 0.5;

	[unroll]
	for (uint slice = 0; slice < GTAO_SLICE_COUNT; ++slice)
	{
		const float phi = ((float(slice) + noise.x) / GTAO_SLICE_COUNT) * PI;
		const float2 slice_direction = float2(cos(phi), sin(phi));
		const float2 omega = slice_direction * sign(pixels_per_unit) * screenspace_radius;

		const float3 direction = float3(slice_direction, 0.0);
		const float3 ortho_direction = direction - (dot(direction, view_vector) * view_vector);
		const float3 axis = normalize(cross(ortho_direction, view_vector));

		const float3 projected_normal = normal - (axis * dot(normal, axis));
		float projected_length = length(projected_normal);

		const float sign_n = sign(dot(ortho_direction, projected_normal));
		const float cos_n = saturate(dot(projected_normal, view_vector) / max(projected_length, 1e-6));
		const float n = sign_n * FastACos(cos_n);

		const float low_horizon_cos0 = cos(n + HALF_PI);
		const float low_horizon_cos1 = cos(n - HALF_PI);

		float horizon_cos0 = low_horizon_cos0;
		float horizon_cos1 = low_horizon_cos1;

		[unroll]
		for (uint step = 0; step < GTAO_STEP_COUNT; ++step)
		{
			const float step_base_noise = float(slice + (step * GTAO_STEP_COUNT)) * 0.6180339887498948482;
			const float step_noise = frac(noise.y + step_base_noise);

			float s = (float(step) + step_noise) / GTAO_STEP_COUNT;
			s = pow(s, Consts.SampleDistributionPower);
			s += min_s;

			const float2 sample_offset = round(s * omega) * pixel_size;

			const float3 delta0 = SampleViewPos(uv + sample_offset) - position;
			const float3 delta1 = SampleViewPos(uv - sample_offset) - position;

			const float distance0 = max(length(delta0), 1e-6);
			const float distance1 = max(length(delta1), 1e-6);

			const float thin_scale = 1.0 + Consts.ThinOccluderCompensation;
			const float falloff_base0 = length(float3(delta0.xy, delta0.z * thin_scale));
			const float falloff_base1 = length(float3(delta1.xy, delta1.z * thin_scale));

			// Nothing is known about the scene past the screen edge. The clamped tap is off the slice, where even a
			// flat wall reads as an occluder, so those taps are left out instead.
			const float2 uv0 = uv + sample_offset;
			const float2 uv1 = uv - sample_offset;
			const float onscreen0 = select(or(any(uv0 < 0.0), any(uv0 > 1.0)), 0.0, 1.0);
			const float onscreen1 = select(or(any(uv1 < 0.0), any(uv1 > 1.0)), 0.0, 1.0);

			const float weight0 = saturate((falloff_base0 * falloff_mul) + falloff_add) * onscreen0;
			const float weight1 = saturate((falloff_base1 * falloff_mul) + falloff_add) * onscreen1;

			const float sample_cos0 = dot(delta0 / distance0, view_vector);
			const float sample_cos1 = dot(delta1 / distance1, view_vector);

			horizon_cos0 = max(horizon_cos0, lerp(low_horizon_cos0, sample_cos0, weight0));
			horizon_cos1 = max(horizon_cos1, lerp(low_horizon_cos1, sample_cos1, weight1));
		}

		projected_length = lerp(projected_length, 1.0, 0.05);

		const float h0 = n + clamp(-FastACos(horizon_cos1) - n, -HALF_PI, HALF_PI);
		const float h1 = n + clamp(FastACos(horizon_cos0) - n, -HALF_PI, HALF_PI);

		const float sin_n = sin(n);
		const float arc0 = (cos_n + (2.0 * h0 * sin_n) - cos((2.0 * h0) - n)) * 0.25;
		const float arc1 = (cos_n + (2.0 * h1 * sin_n) - cos((2.0 * h1) - n)) * 0.25;

		visibility += projected_length * (arc0 + arc1);
	}

	visibility /= GTAO_SLICE_COUNT;
	visibility = pow(visibility, Consts.FinalValuePower);
	visibility = max(0.03, visibility);

	return float2(saturate(visibility / GTAO_OCCLUSION_TERM_SCALE), edges);
}

FSOutput main(FSInput input)
{
	FSOutput output;

	output.vOut = ComputeGTAO(input.vUV);

	return output;
}
