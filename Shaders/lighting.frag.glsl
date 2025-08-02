#version 450

layout(binding = 1) uniform sampler2D s_Depth;
layout(binding = 2) uniform sampler2D s_Albedo;
layout(binding = 3) uniform sampler2D s_Normals;

layout(location = 0) in vec3 a_LightPos;
layout(location = 1) in float a_LightRadius;
layout(location = 2) in vec3 a_PlayerPos;

layout(location = 0) out vec4 v_Color;

layout(push_constant) uniform PushConstants {
    layout(offset = 160) mat4 InvViewMatrix;
    mat4 InvProjMatrix;
} a_PushConsts;

vec3 WorldPosFromDepth(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);

    vec4 clip = a_PushConsts.InvProjMatrix * ndc;
    vec4 view = a_PushConsts.InvViewMatrix * (clip / clip.w);
    vec3 result = view.xyz;

    return result;
}

void main()
{
    vec2 screen_uv = gl_FragCoord.xy / vec2(1024, 720);

    //
    float depth = 1.0 - texture(s_Depth, screen_uv).r;
    vec3 albedo = texture(s_Albedo, screen_uv).rgb;
    vec3 normal = texture(s_Normals, screen_uv).rgb;
    //
    // v_Color = vec4(1, 1, 1, 1);
    // return;

    // depth = 1.0 - depth;
    vec3 world_pos = WorldPosFromDepth(screen_uv, depth);

    vec3 pos_to_light = world_pos - a_LightPos;
    float fdist = length(pos_to_light);
    pos_to_light = normalize(pos_to_light);

    float fdiffuse = max(0.0, dot(normal, -pos_to_light));

    float att_const = 0.35;
    float att_lin = 0.007;
    float att_expr = 0.02;
    float fatt = att_const + att_lin * fdist + att_expr * fdist * fdist;

    vec3 to_light = a_LightPos - world_pos;
    // float distance = length(to_light);

    vec4 pl_color = vec4(1.0, 0.9, 0.75, 0.8);
    float ambient = 0.05;

    vec4 result = pl_color * (ambient + fdiffuse) / fatt;

    // vec3 L = normalize(to_light);
    // vec3 V = normalize(a_PlayerPos - world_pos);
    // vec3 H = normalize(L + V);

    // float light_diff = max(dot(normal, L), 0.0);
    // float light_spec = pow(max(dot(normal, H), 0.0), 72);

    // float attenuation = 1.0 / (1.0 + 0 * distance + 1 * distance * distance);

    // vec3 light_color = vec3(1.0, 0.0, 1.0);

    // vec3 diffuse = albedo * light_diff * light_color;
    // vec3 specular = 3.0 * light_spec * vec3(1, 1, 1);

    // vec3 result = attenuation * (diffuse + specular);
    // vec3 result = vec3(screen_uv, 1);

    v_Color = result;
}
