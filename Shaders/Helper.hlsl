// Dummy macros for the shader preprocessor
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

struct Object
{
	float4x4 mModel;
	float4 UvOffsets;
};

struct Material
{
    uint uiBaseColor;
};

/// Generates UnpackUnorm4x8
[[vk::ext_instruction(64, "GLSL.std.450")]]
float4 F_UnpackUIntToFloat4(uint x);

#define F_TextureName(_name) _name##Texture

#define F_Sample(_name, _coord) F_TextureName(_name).Sample(_name, _coord)
#define F_SampleCmpLevelZero(_name, _texcoord, _zcoord) F_TextureName(_name).SampleCmpLevelZero(_name, _texcoord, _zcoord)

#define F_Texture2D(_name, reg_n) \
    Texture2D F_TextureName(_name) : register(t##reg_n, space0); \
    SamplerState _name : register(s##reg_n, space0);

#define F_ShadowTexture2D(_name, _reg_n) \
    Texture2D F_TextureName(_name) : register(t##_reg_n, space0); \
    SamplerComparisonState _name : register(s##_reg_n, space0);


#define BoneMtx float4x4

#define BONE_COUNT 100
#define LIGHT_COUNT 64
