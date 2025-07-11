#version 450

layout(binding = 1) uniform sampler2D s_Position;
layout(binding = 2) uniform sampler2D s_Albedo;

layout(location = 0) in vec2 a_UV;

layout(location = 0) out vec4 v_Color;

void main()
{
    vec3 frag_pos = texture(s_Position, a_UV).rgb;
    vec4 albedo = texture(s_Albedo, a_UV);

    v_Color = vec4(albedo.rgb, 1.0);
}
