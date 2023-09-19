#version 450
layout(location = 0) in vec2 in_uv;

layout(location = 0) out vec4 out_fragColor;
layout(depth_unchanged) out float gl_FragDepth;

layout(binding = 1) uniform sampler2D u_texture;

void main() {
    out_fragColor = texture(u_texture, in_uv);
}
