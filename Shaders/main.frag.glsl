#version 450

layout(location = 0) out vec4 v_Albedo;
layout(location = 2) out vec4 v_Normal;

layout(location = 0) in vec3 a_Normal;
layout(location = 1) in vec2 a_UV;

layout(set = 1, binding = 0) uniform sampler2D s_Albedo;

void main() {
    v_Albedo = texture(s_Albedo, a_UV);
    v_Normal = vec4(a_Normal, 1.0f);
}
