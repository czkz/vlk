#pragma once
#include <Vector.h>
#include <Quaternion.h>
#include <Matrix.h>

struct Transform {
    Vector3 position;
    Quaternion rotation = Quaternion::Identity();
    Vector3 scale = Vector3(1);

    Matrix4 Matrix() const {
        return
            Vector3(position).TranslationMatrix() *
            rotation.RotationMatrix() *
            scale.ScaleMatrix();
    }

    // Converts from right-handed z-up coordinate system
    // to right-handed z-back coordinate system expected in view space
    static constexpr Matrix4 z_convert = {{
        1,  0, 0, 0,
        0,  0, 1, 0,
        0, -1, 0, 0,
        0,  0, 0, 1,
    }};

    // Converts from right-handed y-up z-back coordinate system
    // to right-handed y-down z-front coordinate system as expected
    // in Vulkan clip space
    static constexpr Matrix4 y_flip = {{
        1,  0,  0, 0,
        0, -1,  0, 0,
        0,  0, -1, 0,
        0,  0,  0, 1,
    }};

    // Expects the camera to point in positive z direction
    static constexpr Matrix4 PerspectiveTransformation(std::pair<float, float> nf) {
        const auto [n, f] = nf;
        return Matrix4({
            n, 0, 0,   0,
            0, n, 0,   0,
            0, 0, f+n, -f*n,
            0, 0, 1,  0,
        });
    }

    // Expects the camera to point in positive z direction,
    // depth is mapped to Vulkan [0, 1] range
    // instead of OpenGL [-1, 1] range.
    static constexpr Matrix4 OrthgraphicProjection(float height, float aspect, std::pair<float, float> near_far) {
        const float h = height;
        const float w = h * aspect;
        const auto [n, f] = near_far;
        const float d = f - n;
        return Matrix4({
            2/w, 0,   0,    0,
            0,   2/h, 0,    0,
            0,   0,   1/d,  -n/d,
            0,   0,   0,    1,
        });
    }

    static constexpr Matrix4 PerspectiveProjection(float fov_deg, float aspect, std::pair<float, float> near_far) {
        const float half_fov = fov_deg * std::numbers::pi / 360;
        const float height = tanf(half_fov) * near_far.first * 2;
        return OrthgraphicProjection(height, aspect, near_far) * PerspectiveTransformation(near_far);
    }
};
