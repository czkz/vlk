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

    static constexpr Matrix4 PerspectiveProjection(float fov_deg, float aspect) {
        const float fov = fov_deg * std::numbers::pi / 180.0;
        const float n = 0.01;
        const float f = 100;
        const float ff = 1.0/tan(fov/2.0);
        return Matrix4({
            ff / aspect, 0,  0,            0,
            0,           ff, 0,            0,
            0,           0,  -(f+n)/(f-n), -2.f*f*n/(f-n),
            0,           0,  -1,           0
        });
    }

    static constexpr Matrix4 OrthgraphicProjection(float height, float aspect) {
        const float h2 = height / 2;
        const float w2 = h2 * aspect;
        const float n = 0;
        const float f = 100;
        return Matrix4({
            1/w2, 0,    0,        0,
            0,    1/h2, 0,        0,
            0,    0,    -2/(f-n), -(f+n)/(f-n),
            0,    0,    0,        1,
        });
    }
};
