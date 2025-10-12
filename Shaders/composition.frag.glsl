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
    // v_Color = vec4(a_UV, 0.0, 1.0);
    // return;

    float inv_depth = texture(s_Depth, a_UV).r;
    // float depth = 1.0 - inv_depth;

    vec4 albedo = texture(s_Albedo, a_UV);
    vec4 normals = texture(s_Normals, a_UV);

    vec4 lights = texture(s_Lights, a_UV);

    // vec3 final_color = lights.rgb;
    // vec3 final_color = WorldPosFromDepth(depth);
    // vec3 final_color = WorldPosFromDepth(depth);
    vec3 base_color = (albedo.rgb);

    // vec3 base_color_with_lighting = base_color * lights.rgb;
    vec3 base_color_with_lighting = lights.rgb;

    // HDR tonemapping
    base_color_with_lighting = base_color_with_lighting / (base_color_with_lighting + vec3(1.0));
    // gamma correct
    const float gamma = 2.2;
    base_color_with_lighting = pow(base_color_with_lighting, vec3(1.0 / gamma));

    if (lights.a <= 1e-5) {
        v_Color = vec4(base_color, 1.0);
    }
    else {
        v_Color = vec4(base_color_with_lighting, 1.0);
    }

    // vec3 final_color = mix(base_color, base_color_with_lighting, lights.a);
    // vec3 final_color = base_color_with_lighting;

    // v_Color = vec4(final_color, 1.0);
}
