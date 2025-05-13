#version 450

layout(location = 0) out vec4 OutColor;

layout(location = 0) in vec3 Normal;

void main() {
    // OutColor = vec4(0.8, 0.6, 0.4, 1.0);
    OutColor = vec4(Normal.xyz, 1.0);
}
