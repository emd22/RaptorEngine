#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 MvpMatrix;
} ubo;

layout(location = 0) out vec3 FragNormal;
layout(location = 1) out vec2 FragTexcoord;

layout(location = 0) in vec3 Position;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 Texcoord;

// layout(push_constant) uniform PushConstants {
//     mat4 MVPMatrix;
// } Constants;

void main() {
    gl_Position = ubo.MvpMatrix * vec4(Position, 1.0);
    FragNormal = Normal;
    FragTexcoord = Texcoord;
}
