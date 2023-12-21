#pragma once
#include <optional>
#include <unordered_set>
#include "Vector.h"

namespace physics {

    struct ContactPoint {
        Vector3 position;
        Vector3 normal;
        float overlap;
    };

    struct CollisionGrid {
        using vec2 = Vector2T<int>;
        struct vec2_hash {
            size_t operator()(const vec2& v) const {
                static_assert(sizeof(int) == 4);
                return std::hash<size_t>()((v.y << 4) | v.x);
            }
        };

        std::unordered_set<vec2, vec2_hash> grid;
        void add(Vector2 pos) {
            grid.insert(quantize(pos));
        }

        std::optional<ContactPoint> checkCollision(const Vector3& pos3d, float r) const {
            const Vector2 pos = pos3d.xy();
            const auto rr = Vector2{r, r};
            const vec2 a = quantize(pos - rr);
            const vec2 b = quantize(pos + rr);
            for (int j = a.y; j <= b.y; j++) {
                for (int i = a.x; i <= b.x; i++) {
                    if (grid.contains({i, j})) {
                        const Vector2 vPos = pos - Vector2(i, j);
                        Vector2 vSurfaceToPos = Vector2(std::abs(vPos.x), std::abs(vPos.y)) - Vector2(blockRadius);
                        if (vSurfaceToPos.x < 0) { vSurfaceToPos.x = 0; }
                        if (vSurfaceToPos.y < 0) { vSurfaceToPos.y = 0; }
                        const float surfaceToPosLen = vSurfaceToPos.Magnitude();
                        const float overlap = r - surfaceToPosLen;
                        if (overlap <= 0) { continue; }
                        if (vPos.x < 0) { vSurfaceToPos.x *= -1; }
                        if (vPos.y < 0) { vSurfaceToPos.y *= -1; }
                        return ContactPoint {
                            .position = Vector3(pos - vSurfaceToPos, 0),
                            .normal = Vector3(vSurfaceToPos / surfaceToPosLen, 0),
                            .overlap = overlap,
                        };
                        // const Vector2 block = Vector2(i, j);
                        // const Vector2 toBlockDirect = block - pos;
                        // const Vector2 toBlock =
                        //     (std::abs(toBlockDirect.x) > std::abs(toBlockDirect.y)
                        //      ? Vector2{toBlockDirect.x, 0}
                        //      : Vector2{0, toBlockDirect.y});
                        // const Vector2 toBlockDir = toBlock.WithMax(1);
                        // const float overlap = blockRadius + r - toBlock.Max();
                        // if (overlap > 0) {
                        //     pos -= toBlockDir * overlap * 1.000001;
                        //     vel = Vector2::ProjectionOnPlane(vel, toBlockDir);
                        //     return true;
                        // }
                    }
                }
            }
            return std::nullopt;
        }

        static constexpr float blockRadius = 0.5;

        static vec2 quantize(Vector2 pos) {
            return vec2(std::round(pos.x), std::round(pos.y));
        }
    };

    inline void resolveCollision(Vector3& pos, Vector3& vel, const ContactPoint& contact) {
        pos += contact.normal * (contact.overlap * 1.000001);
        vel = Vector3::ProjectionOnPlane(vel, contact.normal);
    }

}
