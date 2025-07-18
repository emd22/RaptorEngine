#version 450

layout(binding = 1) uniform sampler2D s_Position;
layout(binding = 2) uniform sampler2D s_Albedo;
layout(binding = 3) uniform sampler2D s_Normals;

layout(binding = 4) uniform sampler2D s_Lights;

layout(location = 0) in vec2 a_UV;

layout(location = 0) out vec4 v_Color;

void main()
{
    vec3 frag_pos = texture(s_Position, a_UV).rgb;
    vec4 albedo = texture(s_Albedo, a_UV);
    vec3 normals = texture(s_Normals, a_UV).rgb;

    vec4 lights = texture(s_Lights, a_UV);

    vec3 final_color = (albedo.rgb * 0.5) + (albedo.rgb * lights.a * lights.rgb);

    v_Color = vec4(final_color, 1.0);
}
