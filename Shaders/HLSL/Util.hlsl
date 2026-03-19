

// Dummy macros for the shader preprocessor
#define F_PROGRAM(_type) _type
#define FPT_VERTEX 0
#define FPT_PIXEL 1

/// Global, copy to all shader types
#define FPT_ALL 2

#define F_REFLECT(_type, _set, _binding) _type
#define FR_STRUCTBUFFER 0
#define FR_UNIFORMBUFFER 1
#define FR_SAMPLER2D 2

#define F_PARAMTEST() ;


struct Object
{
	float4x4 mModel;
};

struct Material
{
    uint uiBaseColor;
};

/// Generates UnpackUnorm4x8
[[vk::ext_instruction(64, "GLSL.std.450")]]
float4 F_UnpackUintToFloat4(uint x);

#define F_TEXTURE_GET(_name) _name##Texture

#define F_SAMPLE(_name, _coord) F_TEXTURE_GET(_name).Sample(_name, _coord)

#define F_TEXTURE2D(_name, reg_n) \
    Texture2D F_TEXTURE_GET(_name) : register(t##reg_n, space0); \
    SamplerState _name : register(s##reg_n, space0);
