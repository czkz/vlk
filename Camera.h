#pragma once
#include <Transform.h>

class SpaceCamera : public Transform {
public:
    void rotateX(float ang) { rotation = rotation * Quaternion::Rotation(ang, {1, 0, 0}); }
    void rotateY(float ang) { rotation = rotation * Quaternion::Rotation(ang, {0, 1, 0}); }
    void rotateZ(float ang) { rotation = rotation * Quaternion::Rotation(ang, {0, 0, 1}); }
};

// class FPSCamera {
// public:
//     Vector3 position;
//     Vector3 euler;
//     /// Negative = no limit
//     float maxPitch = 90 / 180.f * M_PI;
//
//     const Quaternion getRotation() const {
//         return Quaternion::Euler(euler);
//     }
//
//     void ClampPitch() {
//         if (maxPitch > 0) {
//             euler.x = std::clamp<decltype(Vector3::x)>(euler.x, -maxPitch, +maxPitch);
//         }
//     }
//
//     /// Avoids float precision loss at high values by avoiding high values
//     void WrapYaw() {
//         euler.z = fmod(euler.z, 4);
//     }
//
//     void Move(Vector3 localMovement) {
//         Vector3& v = localMovement;
//         const Vector2 i (cos(euler.z), sin(euler.z));
//         position += Vector3(v.x*i.x - v.y*i.y, v.y*i.x + v.x*i.y, v.z);
//     }
// };
