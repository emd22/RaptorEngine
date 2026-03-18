

// Dummy macros for the shader preprocessor
#define F_PROGRAM(_type) _type
#define FPT_VERTEX 0
#define FPT_PIXEL 1

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
