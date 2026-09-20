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

#define ALPHA_CUTOFF 0.5

struct Object
{
	float4x4 mWorld;
};

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


#define BoneMtx float4x4


#define LIGHT_COUNT 64

/// Shadow atlas dimensions. Mirrors ShadowAtlas::scWidth / scHeight in Src/Renderer/ShadowAtlas.hpp, which are
/// compile time constants that ShadowAtlas::GetRegionUVTransform() already bakes into the atlas rects, so the
/// shader does not have to query the texture per pixel per light.
#define SHADOW_ATLAS_WIDTH 4096
#define SHADOW_ATLAS_HEIGHT 2048

#define SSAO_SIZE_DIVISOR 2

#define HAS_FLAG(flags_, has_) (((flags_) & (has_)) != 0)

#define DRAW_FLAG_PROBE_CAPTURE 0x01
#define DRAW_FLAG_DEBUG_PROBE_IRRADIANCE 0x02
#define DRAW_FLAG_DEBUG_PROBE_VISIBILITY 0x04
#define DRAW_FLAG_NO_DECALS 0x08
