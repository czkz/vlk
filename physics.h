#pragma once
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "Vector.h"

namespace std {
    template <> struct hash<Vector2T<int>> {
        size_t operator()(const Vector2T<int>& v) const {
            static_assert(sizeof(int) == 4);
            return hash<size_t>()((v.y << 4) | v.x);
        }
    };
}


struct CollisionGrid {
    using vec2 = Vector2T<int>;

    std::unordered_set<vec2> grid;
    void add(Vector2 pos) {
        grid.insert(quantize(pos));
    }

    bool checkCollision(Vector3& pos, Vector3& vel, float r) const {
        Vector2 p = pos.xy();
        Vector2 v = vel.xy();
        auto ret = checkCollision(p, v, r);
        pos = Vector3(p, pos.z);
        vel = Vector3(v, vel.z);
        return ret;
    }

    bool checkCollision(Vector2& pos, Vector2& vel, float r) const {
        const auto rr = Vector2{r, r};
        const vec2 a = quantize(pos - rr);
        const vec2 b = quantize(pos + rr);
        for (int j = a.y; j <= b.y; j++) {
            for (int i = a.x; i <= b.x; i++) {
                if (grid.contains({i, j})) {
                    const Vector2 block = Vector2(i, j);
                    const Vector2 toBlockDirect = block - pos;
                    const Vector2 toBlock =
                        (std::abs(toBlockDirect.x) > std::abs(toBlockDirect.y)
                         ? Vector2{toBlockDirect.x, 0}
                         : Vector2{0, toBlockDirect.y});
                    const Vector2 toBlockDir = toBlock.WithMax(1);
                    const float overlap = blockRadius + r - toBlock.Max();
                    if (overlap > 0) {
                        pos -= toBlockDir * overlap * 1.000001;
                        vel = Vector2::ProjectionOnPlane(vel, toBlockDir);
                        return true;
                    }
                }
            }
        }
        return false;
    }

    static constexpr float blockRadius = 0.5;

    static vec2 quantize(Vector2 pos) {
        return vec2(std::round(pos.x), std::round(pos.y));
    }
};
