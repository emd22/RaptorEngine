///////////////////////////////////
// Clustered Decals
///////////////////////////////////

/// Mirrors Limits::MaxVisibleDecals
#define MAX_VISIBLE_DECALS 512

/// Each screen tile has one bit per visible decal, packed into this many words. Mirrors Limits::DecalMaskWords.
#define DECAL_MASK_WORDS (MAX_VISIBLE_DECALS / 32)

/// Mirrors `DecalGpuData` on the CPU
struct Decal
{
	// 64
	/// World space to decal space. The decal's box spans [-0.5, 0.5] on every axis in decal space, and +Z points into
	/// the surface.
	float4x4 mWorldToDecal;
	// 80
	/// Center of the box in world space
	float3 vCenter;
	/// Tint in RGB, opacity in A
	uint uiColor;
	// 96
	/// Unit length axes of the box. The shading pass uses the directions, light culling scales them by vHalfExtents.
	float3 vAxisX;
	/// Perceptual roughness that the surface is blended towards
	float1 fRoughness;
	// 112
	float3 vAxisY;
	/// How much of `fRoughness` is blended in, 0 leaves the surface's roughness alone
	float1 fRoughnessWeight;
	// 128
	/// Points into the surface
	float3 vAxisZ;
	/// How far the normal atlas bends the surface's normal, 0 leaves it alone
	float1 fNormalStrength;
	// 144
	/// Decal UV to atlas UV: xy is the scale, zw the offset
	float4 vAtlasRect;
	// 160
	/// World space half extents along each axis, w unused
	float4 vHalfExtents;
};
