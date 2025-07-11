#version 450

// layout(binding = 0) uniform UniformBufferObject {
// mat4 MvpMatrix;
// } StaticUbo;

layout(binding = 1) uniform UniformBufferObject {
    mat4 MvpMatrix;
} Ubo;

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_UV;

layout(location = 0) out vec3 v_Normal;
layout(location = 1) out vec2 v_UV;
layout(location = 2) out vec3 v_Position;

layout(push_constant) uniform PushConstants {
    mat4 MvpMatrix;
    mat4 ModelMatrix;
} a_PushConsts;

void main() {
    vec4 position4 = vec4(a_Position, 1.0);

    gl_Position = a_PushConsts.MvpMatrix * position4;

    vec3 local_position = vec3(a_PushConsts.ModelMatrix * position4);

    v_Normal = a_Normal;
    v_UV = a_UV;
    v_Position = local_position;
}
