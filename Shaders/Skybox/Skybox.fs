#version 450

layout(set = 0, binding = 0) uniform samplerCube s_Sky;

layout(location = 0) in vec3 a_UVW;

layout(location = 0) out vec4 v_FragColor;

void main()
{
    v_FragColor = texture(s_Sky, a_UVW);
}
