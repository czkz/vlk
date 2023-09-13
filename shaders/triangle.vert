#version 450

layout (location = 0) in vec3 a_position;
layout (location = 1) in vec2 a_uv;

layout (location = 0) out vec3 fragColor;

layout(binding = 0) uniform UniformBufferObject {
    mat4 MVP;
} ubo;

void main() {
    fragColor = vec3(a_uv, 0.0);
    gl_Position = ubo.MVP * vec4(a_position, 1.0);
}
