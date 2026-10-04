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
	// 80
	/// Direction towards the light for directional lights, already normalized. World position otherwise.
	float3 vLightPosition;
	float1 fLightRadius;
	// 96
	uint1 uiLightColor;
	uint1 uiLightType;
	float1 fIntensity;
	/// 1 / (radius * radius), so the shading pass does not divide once per pixel per light
	float1 fInvRadiusSq;
	// 112
	/// Spot lights only: world space direction the cone points along
	float3 vSpotDirection;
	/// Spot lights only: cosine of the outer cone half-angle
	float1 fSpotCosOuter;
	// 128
	float3 vLightColor;
	/// Spot lights only: 1 / (cos(inner) - cos(outer))
	float1 fSpotAngleScale;
	// 144
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
	/// The words of this tile's decal mask that have any bits set, [DecalWordStart, DecalWordEnd)
	uint DecalWordStart;
	uint DecalWordEnd;
};


#define FX_MATH_PI 3.14159265359
#define FX_MATH_1_OVER_PI 0.31830988618

#define PROBE_CAPTURE_AMBIENT_ILLUMINANCE 1500.0

/// Clamped dot product
float DotC(float3 a, float3 b)
{
	return max(dot(a, b), 1e-5);
}


float3 SrgbToLinear(float3 srgb)
{
	const float3 low = srgb / 12.92;
	const float3 high = pow((srgb + 0.055) / 1.055, 2.4);

	return lerp(high, low, step(srgb, 0.04045));
}

float Pow5(float x)
{
	const float x2 = x * x;
	return x2 * x2 * x;
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
	return f0 + (f90 - f0) * Pow5(1.0 - u);
}


float Fr_FrostbiteDisneyDiffuse(float NdotV, float NdotL, float LdotH, float linear_roughness)
{
	float energy_bias = 0.5 * linear_roughness;
	float energy_factor = lerp(1.0, 1.0 / 1.51, linear_roughness);

	float fd90_minus_one = energy_bias + 2.0 * LdotH * LdotH * linear_roughness - 1.0;

	float light_scatter = 1.0 + (fd90_minus_one * Pow5(1.0 - NdotL));
	float view_scatter = 1.0 + (fd90_minus_one * Pow5(1.0 - NdotV));

	return light_scatter * view_scatter * energy_factor;
}

#define DFG_LUT_SIZE 32.0

float2 SampleDfg(Texture2D lut, SamplerState lut_sampler, float NdotV, float roughness)
{
	const float2 coords = saturate(float2(NdotV, roughness));
	const float2 uv = (coords * ((DFG_LUT_SIZE - 1.0) / DFG_LUT_SIZE)) + (0.5 / DFG_LUT_SIZE);

	return lut.SampleLevel(lut_sampler, uv, 0.0).rg;
}

float3 SpecularEnergyCompensation(float3 f0, float2 dfg)
{
	return 1.0 + (f0 * ((1.0 / max(dfg.x + dfg.y, 1e-3)) - 1.0));
}

float3 MultiScatterReflectance(float3 f0, float3 single_scatter, float2 dfg)
{
	const float missing = 1.0 - (dfg.x + dfg.y);
	const float3 average_fresnel = f0 + ((1.0 - f0) / 21.0);

	return (single_scatter * average_fresnel * missing) / (1.0 - (average_fresnel * missing));
}

/// Occlusion of specular ambient light (Lagarde & de Rousiers, "Moving Frostbite to PBR"). A cavity blocks the
/// reflection less than it blocks diffuse light at grazing angles and on smooth surfaces.
float SpecularOcclusion(float NdotV, float ambient_occlusion, float alpha)
{
	return saturate(pow(NdotV + ambient_occlusion, exp2(-16.0 * alpha - 1.0)) - 1.0 + ambient_occlusion);
}

float3 MultiBounceOcclusion(float visibility, float3 albedo)
{
	const float3 a = (2.0404 * albedo) - 0.3324;
	const float3 b = (-4.7951 * albedo) + 0.6417;
	const float3 c = (2.7552 * albedo) + 0.6903;

	return max(visibility, ((((visibility * a) + b) * visibility) + c) * visibility);
}

/// Direction the specular lobe is centred on, for looking up ambient specular (Frostbite,
/// "Moving Frostbite to PBR", getSpecularDominantDir). A mirror reflects along R, but as a surface roughens its
/// lobe spreads over the hemisphere and its average direction swings towards the normal. Sampling along R
/// regardless makes rough surfaces read the environment from a direction their lobe barely covers.
float3 GetSpecularDominantDir(float3 N, float3 R, float alpha)
{
	const float smoothness = saturate(1.0 - alpha);
	const float factor = smoothness * (sqrt(smoothness) + alpha);

	return normalize(lerp(N, R, factor));
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
