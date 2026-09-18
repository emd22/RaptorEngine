
/// Mirrors eMaterialFlags in Src/Material/Material.hpp
#define MF_UNLIT (1 << 0)
#define MF_SPECULAR_GLOSSINESS (1 << 1)

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
};
