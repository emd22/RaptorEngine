#ifndef FOG_CONSTS_HLSLI
#define FOG_CONSTS_HLSLI

struct FogConsts
{
	float4x4 mFarSunMatrix;
	float4x4 mPrevCameraMatrix;
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
	uint uiPad0;
	uint uiFlags;
	float fJitterOffset;
	uint uiNoiseShift;
	float2 vScreenSize;
	float fHistoryBlend;
	float fJitterAmount;
	float4 vBakedShadowRight;
	float4 vBakedShadowUp;
	float4 vBakedShadowForward;
	float4 vBakedShadowGrid;
	float4 vBakedShadowAtlas;
};


struct FogShadowPage
{
	float4 vLightRect;
	float4 vAtlasRect;
	float4 vDepth;
};

/// Mirrors the BakedShadow* members of FogConsts
struct BakedSunShadowParams
{
	/// XYZ are the light space axes. W: page size, depth bias, PCF radius.
	float4 vRight;
	float4 vUp;
	float4 vForward;
	/// First cell XY, cell counts ZW
	float4 vGrid;
	/// Reciprocal atlas size in XY
	float4 vAtlas;
};

#define FOG_FLAG_FAR_SUN_SHADOW 0x02
#define FOG_FLAG_NEAR_SUN_DYNAMIC_ONLY 0x04
#define FOG_FLAG_BAKED_SUN_SHADOW 0x08
#define FOG_FLAG_DEBUG_NO_CLAMP 0x10
#define FOG_FLAG_DEBUG_ACCEPT_ALL 0x20
#define FOG_FLAG_DEBUG_SLOPE_ACCEPTANCE 0x40

#define FOG_CASCADE_FADE 0.1

#define FOG_SHADOW_BIAS -0.000005f

#define FOG_PCF_RADIUS 1.5

#define FROXEL_GRID_X 160
#define FROXEL_GRID_Y 90
#define FROXEL_GRID_Z 64

#endif
