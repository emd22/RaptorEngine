/// Mirrors `eLightType` on the CPU
#define FX_LIGHT_TYPE_UNKNOWN 0
#define FX_LIGHT_TYPE_DIRECTIONAL 1
#define FX_LIGHT_TYPE_POINT 2
#define FX_LIGHT_TYPE_SPOT 3

/// Mirrors `LightGpuData` on the CPU
struct Light
{
	// 64
	/// View projection matrix of the light's shadow map, for lights with a region in the shadow atlas
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
	// 240
	float2 vCameraSize;
	uint1 uiAmbient;
	uint1 uiLightType;
	// 256
	/// Spot lights only: world space direction the cone points along
	float3 vSpotDirection;
	/// Spot lights only: cosine of the outer cone half-angle
	float1 fSpotCosOuter;
	// 272
	/// Spot lights only: 1 / (cos(inner) - cos(outer))
	float1 fSpotAngleScale;
	float1 _fPad0;
	float2 _vPad1;
	// 288
	/// Shadow map UV to shadow atlas UV: xy is the scale, zw the offset. Zero when the light casts no shadows.
	float4 vShadowAtlasRect;
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

/// Analytic fit of the split-sum environment BRDF (Karis, "Physically Based Shading on Mobile").
/// Scales prefiltered ambient radiance into reflected specular for the given F0 and perceptual roughness.
float3 EnvBRDFApprox(float3 f0, float roughness, float NdotV)
{
	const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
	const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);

	float4 r = roughness * c0 + c1;
	float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
	float2 AB = float2(-1.04, 1.04) * a004 + r.zw;

	return f0 * AB.x + AB.y;
}

float AttenuationSmooth(float distance_sq, float inv_radius_sq)
{
	float factor = distance_sq * inv_radius_sq;
	float smooth_factor = saturate(1.0 - factor * factor);

	return (smooth_factor * smooth_factor) / max(distance_sq, 1e-4);
}

/// Angular falloff of a spot light cone. `L` is the normalized surface to light vector.
/// Full intensity inside the inner cone, fading smoothly to zero at the outer cone.
float AttenuationSpot(float3 L, Light light)
{
	float cos_angle = dot(-L, light.vSpotDirection);
	float factor = saturate((cos_angle - light.fSpotCosOuter) * light.fSpotAngleScale);

	return factor * factor;
}

/// Sphere that tightly encloses a spot light's cone (Wronski, "Cull that cone").
/// Returns the center in xyz and the radius in w.
float4 GetSpotBoundingSphere(Light light)
{
	const float cos_outer = light.fSpotCosOuter;
	const float range = light.fLightRadius;

	// Wide cones (over 45 degrees): the sphere centered on the cap's base circle
	if (cos_outer < 0.70710678) {
		const float sin_outer = sqrt(saturate(1.0 - cos_outer * cos_outer));
		return float4(light.vLightPosition + light.vSpotDirection * (range * cos_outer), range * sin_outer);
	}

	// Narrow cones: the sphere passing through the apex and the cap's base circle
	const float radius = range / (2.0 * cos_outer);
	return float4(light.vLightPosition + light.vSpotDirection * radius, radius);
}
