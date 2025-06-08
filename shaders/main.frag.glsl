#version 450

layout(location = 0) out vec4 OutColor;

layout(location = 0) in vec3 Normal;
layout(location = 1) in vec2 Texcoord;

// layout(set = 0, binding = 0) uniform sampler2D Texture;

void main() {
    // OutColor = texture(Texture, UV);
    OutColor = vec4(Texcoord, 0.0, 1.0);
    // OutColor = vec4(Normal.xyz, 1.0);
}
