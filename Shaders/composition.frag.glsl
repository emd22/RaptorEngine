#version 450

layout(binding = 1) uniform sampler2D s_Depth;
layout(binding = 2) uniform sampler2D s_Albedo;
layout(binding = 3) uniform sampler2D s_Normals;
layout(binding = 4) uniform sampler2D s_Lights;

layout(location = 0) in vec2 a_UV;

layout(location = 0) out vec4 v_Color;

layout(push_constant) uniform PushConstants {
    mat4 InvViewMatrix;
    mat4 InvProjMatrix;
} a_PushConsts;

vec3 WorldPosFromDepth(float depth) {
    vec4 ndc = vec4(a_UV * 2.0 - 1.0, depth, 1.0);

    vec4 clip = a_PushConsts.InvProjMatrix * ndc;
    vec4 view = a_PushConsts.InvViewMatrix * (clip / clip.w);
    vec3 result = view.xyz;

    return result;
}

void main()
{
    // v_Color = vec4(a_UV, 1.0, 1.0);
    // return;
    float depth = texture(s_Depth, a_UV).r;
    vec4 albedo = texture(s_Albedo, a_UV);
    vec3 normals = texture(s_Normals, a_UV).rgb;

    vec4 lights = texture(s_Lights, a_UV);

    // vec3 final_color = lights.rgb;
    vec3 final_color = albedo.rgb;
    // vec3 final_color = WorldPosFromDepth(depth);
    // vec3 final_color = (albedo.rgb * (lights.a * lights.rgb));

    v_Color = vec4(final_color, 1.0);
}
