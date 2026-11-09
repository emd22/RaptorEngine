// Dummy macros for the shader preprocessor
#pragma pack_matrix(row_major)
#define F_PROGRAM(_type) ;
#define FPT_VERTEX 0
#define FPT_PIXEL 1
/// Global, copy to all shader types
#define FPT_ALL 2

#define F_REFLECT(_type, _binding, _set) ;

#define FR_STRUCTBUFFER 0
#define FR_CBUFFER 1
#define FR_SAMPLER2D 2

#define F_PARAMTEST() ;

// Dummy permutation stuff, handled by ShaderPreproc.
#define PERMIF(x_) ;
#define PERMNOT(x_) ;
#define PERMELSE() ;
#define PERMEND() ;

/// Default cutoff for opaque and alpha masked materials. Pipelines that need a different one (the transparent
/// variants drop it to near zero) pass ALPHA_CUTOFF as a shader macro, which the preprocessor defines ahead of
/// this include, so the guard has to stay.
#ifndef ALPHA_CUTOFF
#define ALPHA_CUTOFF 0.5
#endif

struct Object
{
	float4x4 mWorld;
	float4x4 mPrevWorld;
};

/// `v * m` as an explicit chain of fused multiply-adds, which leaves the shader compiler nothing to fuse its own way, so
/// any shader that transforms a point with it gets the same bits whatever else it computes. The prepass and the forward
/// pass rely on it, since the forward pass only shades where its depth is EQUAL to the prepass's. (`precise` would do
/// the same, but ZDXC drops it.)
float4 MulPointExact(float4 v, float4x4 m) { return mad(v.x, m[0], mad(v.y, m[1], mad(v.z, m[2], v.w * m[3]))); }

float4 ProjectPointExact(float4 position_ms, float4x4 world, float4x4 view_projection)
{
	return MulPointExact(MulPointExact(position_ms, world), view_projection);
}

/// A skinned position from four weighted bone matrices, exact in the same way as MulPointExact
float4 SkinPointExact(float4 position, float4x4 bone0, float4x4 bone1, float4x4 bone2, float4x4 bone3, float4 weights)
{
	return mad(weights.x, MulPointExact(position, bone0),
			   mad(weights.y, MulPointExact(position, bone1),
				   mad(weights.z, MulPointExact(position, bone2), weights.w * MulPointExact(position, bone3))));
}

/// Generates UnpackUnorm4x8
[[vk::ext_instruction(64, "GLSL.std.450")]]
float4 F_UnpackUIntToFloat4(uint x);

#define F_TextureName(_name) _name##Texture

#define F_Sample(_name, _coord) F_TextureName(_name).Sample(_name, _coord)
#define F_SampleLoad(_name, _coord) F_TextureName(_name).Load(_coord)
#define F_SampleCmpLevelZero(_name, _texcoord, _zcoord) F_TextureName(_name).SampleCmpLevelZero(_name, _texcoord, _zcoord)
#define F_SampleGrad(_name, _coord, _ddx, _ddy) F_TextureName(_name).SampleGrad(_name, _coord, _ddx, _ddy)
#define F_SampleLevel(_name, _coord, _lod) F_TextureName(_name).SampleLevel(_name, _coord, _lod)

#define F_Texture2D(_name, binding_, set_) \
    Texture2D F_TextureName(_name) : register(t##binding_, space##set_); \
    SamplerState _name : register(s##binding_, space##set_);

#define F_TextureCubeArray(_name, binding_, set_) \
    TextureCubeArray F_TextureName(_name) : register(t##binding_, space##set_); \
    SamplerState _name : register(s##binding_, space##set_);

#define F_Texture3D(_name, binding_, set_) \
    Texture3D F_TextureName(_name) : register(t##binding_, space##set_); \
    SamplerState _name : register(s##binding_, space##set_);

#define F_RWTexture3D(_name, type_, binding_, set_) \
    [[vk::binding(binding_, set_)]] RWTexture3D<type_> _name

#define F_DataTexture2D(_name, type_, binding_, set_) \
	Texture2D<type_> F_TextureName(_name) : register(t##binding_, space##set_); \

#define F_ShadowTexture2D(_name, binding_, set_) \
    Texture2D F_TextureName(_name) : register(t##binding_, space##set_); \
    SamplerComparisonState _name : register(s##binding_, space##set_);

#define F_StructBuffer(name_, obj_type_, binding_, set_) \
    [[vk::binding(binding_, set_)]] StructuredBuffer<obj_type_> name_

#define F_RWStructBuffer(name_, obj_type_, binding_, set_) \
    [[vk::binding(binding_, set_)]] RWStructuredBuffer<obj_type_> name_

#define F_CBuffer(name_, binding_, set_) \
    [[vk::binding(binding_, set_)]] cbuffer name_

#define F_FrameData(type_, name_, set_) \
    [[vk::binding(0, set_)]] ConstantBuffer<type_> name_


#define BoneMtx float4x4


#define LIGHT_COUNT 64

/// Shadow atlas dimensions. Mirrors ShadowAtlas::scAtlasSize in Src/Renderer/ShadowAtlas.hpp, which is
/// compile time constants that ShadowAtlas::GetRegionUVTransform() already bakes into the atlas rects, so the
/// shader does not have to query the texture per pixel per light.
#define SHADOW_ATLAS_WIDTH 4096
#define SHADOW_ATLAS_HEIGHT 2048

#define SSAO_SIZE_DIVISOR 2

#define HAS_FLAG(flags_, has_) (((flags_) & (has_)) != 0)



///////////////////////////////////////
// Draw Flags
///////////////////////////////////////

#define DRAW_FLAG_PROBE_CAPTURE 0x01
#define DRAW_FLAG_DEBUG_PROBE_IRRADIANCE 0x02
#define DRAW_FLAG_DEBUG_PROBE_VISIBILITY 0x04
#define DRAW_FLAG_NO_DECALS 0x08
#define DRAW_FLAG_PROBE_BOUNCE 0x20
#define DRAW_FLAG_NO_REFLECTION_PROBES 0x40
#define DRAW_FLAG_REFLECTION_CAPTURE 0x80
#define DRAW_FLAG_DEBUG_REFLECTION 0x100
#define DRAW_FLAG_DEBUG_REFLECTION_COVERAGE 0x200
#define DRAW_FLAG_BAKED_SUN 0x400
#define DRAW_FLAG_BAKED_INDIRECT 0x800
#define DRAW_FLAG_NO_REFLECTION_NORMALIZE 0x1000
#define DRAW_FLAG_DEBUG_REFLECTION_NORMALIZE 0x2000
