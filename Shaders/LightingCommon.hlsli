/// Mirrors `eLightType` on the CPU
#define FX_LIGHT_TYPE_UNKNOWN 0
#define FX_LIGHT_TYPE_DIRECTIONAL 1
#define FX_LIGHT_TYPE_POINT 2

struct Light
{
	// 64
	float4x4 LightCameraMatrix;
	// 128
	float4x4 mInvView;
	// 192
	float4x4 mInvProjection;
	// 208
	float3 vEyePosition;
	float1 fLightRadius;
	// 224
	float3 vLightPosition;
	uint1 uiLightColor;
	// 236
	float2 vCameraSize;
	uint1 uiAmbient;
	uint1 uiLightType;
};

///////////////////////////////////
// Forward+ Tiled Lighting
///////////////////////////////////

#define LIGHT_TILE_SIZE 16
#define MAX_LIGHTS_PER_TILE 6

/// Per tile light list offsets. `StartIndex` points into the global light index list.
struct TileLightData
{
	uint Count;
	uint StartIndex;
};


#define FX_MATH_PI 3.14159265359
#define FX_MATH_1_OVER_PI 0.31830988618

/// Clamped dot product
float DotC(float3 a, float3 b)
{
	return max(dot(a, b), 1e-5);
}


float D_GGX(float NdotH, float m)
{
	float m2 = m * m;
	float f = (NdotH * m2 - NdotH) * NdotH + 1.0;
	return m2 / (f * f);
}

float GeometrySchlickBeckmann(float cos_theta, float K)
{
	return (cos_theta) / (cos_theta * (1.0 - K) + K);
}

float V_SmithGGXCorrelated(float NdotL, float NdotV, float alphaG)
{
	float alphaG2 = alphaG * alphaG;

	float L_GGXV = NdotL * sqrt((-NdotV * alphaG2 + NdotV) * NdotV + alphaG2);
	float L_GGXL = NdotV * sqrt((-NdotL * alphaG2 + NdotL) * NdotL + alphaG2);

	return 0.5f / (L_GGXV + L_GGXL);
}

float3 F_Schlick(float3 f0, float f90, float u)
{
	return f0 + (f90 - f0) * pow(1.0 - u, 5.0);
}


float Fr_FrostbiteDisneyDiffuse(float NdotV, float NdotL, float LdotH, float linear_roughness)
{
	float energy_bias = 0.5 * linear_roughness;
	float energy_factor = lerp(1.0, 1.0 / 1.51, linear_roughness);

	float fd90_minus_one = energy_bias + 2.0 * LdotH * LdotH * linear_roughness - 1.0;

	float light_scatter = 1.0 + (fd90_minus_one * pow(1.0 - NdotL, 5.0));
	float view_scatter = 1.0 + (fd90_minus_one * pow(1.0 - NdotV, 5.0));

	return light_scatter * view_scatter * energy_factor;
}

float AttenuationSmooth(float distance_sq, float inv_radius_sq)
{
	float factor = distance_sq * inv_radius_sq;
	float smooth_factor = saturate(1.0 - factor * factor);

	return (smooth_factor * smooth_factor) / max(distance_sq, 1e-4);
}
