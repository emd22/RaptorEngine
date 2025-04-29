#version 450

layout(location = 0) in vec3 Position;
layout(location = 1) in vec3 Normal;

layout(push_constant) uniform PushConstants {
    mat4 MVPMatrix;
} Constants;

void main() {
    gl_Position = Constants.MVPMatrix * vec4(Position, 1.0);
}
