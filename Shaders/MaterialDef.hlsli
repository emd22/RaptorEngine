
/// Mirrors eMaterialFlags in Src/Material/Material.hpp
#define MF_UNLIT (1 << 0)
#define MF_SPECULAR_GLOSSINESS (1 << 1)
#define MF_ALPHA_MASK (1 << 2)
#define MF_DOUBLE_SIDED (1 << 3)

/// Mirrors MaterialProperties in Src/Material/Material.hpp
struct Material
{
	uint Flags;
	float fAlpha;

	/// Metallic/roughness workflow: scale the metallic (B) and roughness (G) texture channels.
	float fMetallicFactor;
	float fRoughnessFactor;

	/// Specular/glossiness workflow (MF_SPECULAR_GLOSSINESS): scale the specular (RGB) and glossiness (A) texture
	/// channels.
	float3 vSpecularFactor;
	float fGlossinessFactor;

	float3 vBaseColorFactor;

	/// How much of the surface texture's R channel is applied as ambient occlusion
	float fOcclusionStrength;

	float3 vEmissiveFactor;
	float fAlphaCutoff;
};

float GetAlphaCutoff(Material material)
{
	return select(HAS_FLAG(material.Flags, MF_ALPHA_MASK), material.fAlphaCutoff, ALPHA_CUTOFF);
}
