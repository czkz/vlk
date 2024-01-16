#version 450
layout (location = 0) in vec3 in_position;
layout (location = 1) in vec2 in_uv;

layout(location = 0) out vec2 out_uv;

layout(push_constant) uniform PushConstants {
    mat4 u_MVP;
};

void main() {
    out_uv = in_uv;
    gl_Position = u_MVP * vec4(in_position, 1.0);
}
