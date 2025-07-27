#version 450

layout(binding = 1) uniform sampler2D s_Depth;
layout(binding = 2) uniform sampler2D s_Albedo;
layout(binding = 3) uniform sampler2D s_Normals;

layout(location = 0) out vec4 v_Color;

// vec3 WorldPosFromDepth(vec2 uv, float depth)
// {
// vec3 ndc = vec3(uv, depth) * 2.0 - 1.0;
// vec4 world = a_InvVP * vec4(ndc, 1.0);
// vec3 world_position = world.xyz / world.w;
// return world_position;

// // Light Pixel shader
// vec3 view_ray = vec3(a_ObjPos.xy / a_ObjPos.z, 1.0f);

// // Sample the depth and convert to linear view space Z (assume it gets sampled as
// // a floating point value of the range [0,1])
// float lin_depth = a_ProjB / (depth - a_ProjA);
// vec3 positionVS = view_ray * lin_depth;

// return positionVS;
// }
//

// vec4 CalcLightInternal(vec3 light_direction, vec3 normal)
// {
//     vec4 AmbientColor = vec4(0.8, 0.8, 0.8, 1.0f) * 0.025;
//     float DiffuseFactor = dot(Normal, -LightDirection);

//     vec4 DiffuseColor = vec4(0, 0, 0, 0);
//     vec4 SpecularColor = vec4(0, 0, 0, 0);

//     if (DiffuseFactor > 0) {
//         DiffuseColor = vec4(vec3(0.9, 0.9, 0.8) * 0.9 * DiffuseIntensity * DiffuseFactor, 1.0f);

//         vec3 VertexToEye = normalize(gEyeWorldPos - WorldPos0);
//         vec3 LightReflect = normalize(reflect(light_direction, Normal));
//         float SpecularFactor = dot(VertexToEye, LightReflect);

//         if (SpecularFactor > 0) {
//             SpecularFactor = pow(SpecularFactor, gSpecularPower);
//             SpecularColor = vec4(Light.Color * gMatSpecularIntensity * SpecularFactor, 1.0f);
//         }
//     }

//     return (AmbientColor + DiffuseColor + SpecularColor);
// }

// vec4 CalcPointLight(int Index, vec3 Normal)
// {
//     vec3 LightDirection = WorldPos0 - gPointLights[Index].Position;
//     float Distance = length(LightDirection);
//     LightDirection = normalize(LightDirection);

//     vec4 Color = CalcLightInternal(vec3(5, 5, 5), Normal);
//     float Attenuation = 0.08 +
//             gPointLights[Index].Atten.Linear * Distance +
//             gPointLights[Index].Atten.Exp * Distance * Distance;

//     return Color / Attenuation;
// }

void main()
{

    // v_Color = vec4(1.0, 1.0, 1.0, 0.25);
    v_Color = vec4(1, 1, 1, 0.6);
    //
    // v_Color = vec4(a_Position, 1.0);

    // v_Color = vec4(WorldPosFromDepth(a_UV, depth), 1.0f);
}
