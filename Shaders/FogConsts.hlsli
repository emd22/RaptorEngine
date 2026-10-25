struct FogConsts
{
	float4x4 mFarSunMatrix;
	float4 vFarSunAtlasRect;
	float4 vRays[4];
	float4 vOriginAndDensity;
	float4 vCameraOffsetAndHaze;
	float4 vAlbedoAndFalloff;
	float fDepthA;
	float fDepthB;
	float fMarchDistance;
	float fPreExposure;
	float fAnisotropy;
	float fAmbientScale;
	uint uiTileColumns;
	uint uiTileRows;
	uint uiStepCount;
	uint uiFlags;
	float fJitterOffset;
	uint uiNoiseShift;
	float2 vScreenSize;
};

#define FOG_FLAG_PROBE_SUN_VISIBILITY 0x01
#define FOG_FLAG_FAR_SUN_SHADOW 0x02
#define FOG_FLAG_NEAR_SUN_DYNAMIC_ONLY 0x04

#define FOG_CASCADE_FADE 0.1

#define FOG_FAST_SEGMENTS 8

#define FOG_SHADOW_BIAS -0.000005f

#define FOG_PCF_RADIUS 1.5

#define FOG_SH_L0_TO_RADIANCE (0.282095 * FX_MATH_1_OVER_PI)

#define FROXEL_GRID_X 160
#define FROXEL_GRID_Y 90
#define FROXEL_GRID_Z 64
