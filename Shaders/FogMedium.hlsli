struct FogRay
{
	float3 vOrigin;
	float3 vDirection;
	float fOriginDensity;
	float fFalloffAlongRay;
	float fFalloff;
	float fHaze;
};

FogRay MakeFogRay(float3 direction)
{
	FogRay ray;

	ray.vOrigin = Consts.vOriginAndDensity.xyz;
	ray.vDirection = direction;
	ray.fFalloff = Consts.vAlbedoAndFalloff.w;
	ray.fFalloffAlongRay = ray.fFalloff * direction.y;
	ray.fHaze = Consts.vCameraOffsetAndHaze.w;
	ray.fOriginDensity = Consts.vOriginAndDensity.w;

	return ray;
}

float FogOpticalDepth(FogRay ray, float t)
{
	const float x = max(ray.fFalloffAlongRay * t, -80.0);
	const float exact = (1.0 - exp(-x)) / ray.fFalloffAlongRay;
	const float series = t * (1.0 - (0.5 * x));
	const float integral = select(abs(x) < 1e-4, series, exact);

	return (ray.fOriginDensity * integral) + (ray.fHaze * t);
}

float FogTransmittance(FogRay ray, float t)
{
	return exp(-FogOpticalDepth(ray, t));
}

float PhaseHenyeyGreenstein(float cos_theta, float g)
{
	const float g2 = g * g;
	const float denom = max(1.0 + g2 - (2.0 * g * cos_theta), 1e-4);

	return (1.0 - g2) / (4.0 * FX_MATH_PI * denom * sqrt(denom));
}

float LinearViewDepth(float raw_depth)
{
	return Consts.fDepthB / max(raw_depth - Consts.fDepthA, 1e-12);
}

float3 FogEyePosition()
{
	return Consts.vOriginAndDensity.xyz + Consts.vCameraOffsetAndHaze.xyz;
}

FogRay MakeEyeFogRay(float3 direction)
{
	FogRay ray = MakeFogRay(direction);

	const float3 eye = FogEyePosition();
	const float height_offset = eye.y - ray.vOrigin.y;

	ray.vOrigin = eye;
	ray.fOriginDensity *= exp(clamp(-ray.fFalloff * height_offset, -80.0, 80.0));

	return ray;
}

float3 FogScreenRay(float2 uv)
{
	const float3 ray_top = lerp(Consts.vRays[0].xyz, Consts.vRays[1].xyz, uv.x);
	const float3 ray_bottom = lerp(Consts.vRays[2].xyz, Consts.vRays[3].xyz, uv.x);

	return lerp(ray_top, ray_bottom, uv.y);
}

float FroxelSliceToDepth(float slice)
{
	const float u = slice / float(FROXEL_GRID_Z);

	return Consts.fMarchDistance * u * u;
}

float FroxelDepthToSlice(float view_depth)
{
	return float(FROXEL_GRID_Z) * sqrt(saturate(view_depth / Consts.fMarchDistance));
}
