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
    // to right-handed z-back coordinate system expected in clip space
    static constexpr Matrix4 z_convert = {{
        1,  0, 0, 0,
        0,  0, 1, 0,
        0, -1, 0, 0,
        0,  0, 0, 1,
    }};

    static constexpr Matrix4 PerspectiveTransformation(std::pair<float, float> nf) {
        const auto [n, f] = nf;
        return Matrix4({
            n, 0, 0,   0,
            0, n, 0,   0,
            0, 0, f+n, f*n,
            0, 0, -1,  0,
        });
    }

    static constexpr Matrix4 OrthgraphicProjection(float height, float aspect, std::pair<float, float> nf) {
        const float h = height;
        const float w = h * aspect;
        const auto [n, f] = nf;
        const float d = f - n;
        return Matrix4({
            2/w, 0,   0,    0,
            0,   2/h, 0,    0,
            0,   0,   -1/d, -n/d,
            0,   0,   0,    1,
        });
    }

    static constexpr Matrix4 PerspectiveProjection(float fov_deg, float aspect, std::pair<float, float> nf) {
        float fov2 = fov_deg * std::numbers::pi / 180.f / 2.f;
        return OrthgraphicProjection((tanf(fov2) * nf.first) * 2.f, aspect, nf) * PerspectiveTransformation(nf);
    }
};
