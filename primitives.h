#pragma once
#include <Vector.h>
#include <Quaternion.h>
#include <memory>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>


namespace primitives::detail {

    inline void generate_quad(Vector2 a, Vector2 b, std::vector<Vector3>& out) {
        out.emplace_back( a.x, a.y, 0.5 );
        out.emplace_back( b.x, a.y, 0.5 );
        out.emplace_back( a.x, b.y, 0.5 );
        out.emplace_back( a.x, b.y, 0.5 );
        out.emplace_back( b.x, a.y, 0.5 );
        out.emplace_back( b.x, b.y, 0.5 );
    }

    inline void generate_side(int sub, std::vector<Vector3>& out) {
        for (int i = 0; i < sub; i++) {
            for (int j = 0; j < sub; j++) {
                generate_quad(
                    {float(i+0)/sub-0.5f, float(j+0)/sub-0.5f},
                    {float(i+1)/sub-0.5f, float(j+1)/sub-0.5f},
                    out
                );
            }
        }
    }

    inline void generate_cube_pos(std::vector<Vector3>& out_pos, int subdivisions) {
        size_t side_verts = 6 * subdivisions * subdivisions;
        out_pos.reserve(side_verts * 6);
        auto rotations = std::to_array({
            Quaternion::Identity(),
            Quaternion::Rotation(std::numbers::pi, {0, +1, 0}),
            Quaternion::Rotation(std::numbers::pi / 2, {+1, 0, 0}),
            Quaternion::Rotation(std::numbers::pi / 2, {-1, 0, 0}),
            Quaternion::Rotation(std::numbers::pi / 2, {0, +1, 0}),
            Quaternion::Rotation(std::numbers::pi / 2, {0, -1, 0}),
        });
        // Generate side 0
        generate_side(subdivisions, out_pos);
        // Generate others
        for (int side = 1; side < 6; side++) {
            for (size_t i = 0; i < side_verts; i++) {
                out_pos.push_back(rotations[side].Rotate(out_pos[i]));
            }
        }
    }

    inline void generate_cube_normals(std::vector<Vector3>& out_norms, int subdivisions) {
        size_t side_verts = 6 * subdivisions * subdivisions;
        out_norms.reserve(side_verts * 6);
        auto normals = std::to_array({
            Vector3 { 0, 0, +1 },
            Vector3 { 0, 0, -1 },
            Vector3 { 0, -1, 0 },
            Vector3 { 0, +1, 0 },
            Vector3 { +1, 0, 0 },
            Vector3 { -1, 0, 0 },
        });
        for (int side = 0; side < 6; side++) {
            for (size_t i = 0; i < side_verts; i++) {
                out_norms.push_back(normals[side]);
            }
        }
    }

    inline void generate_sphere_normals(std::vector<Vector3>& out_pos, std::vector<Vector3>& out_norms, int subdivisions) {
        size_t side_verts = 6 * subdivisions * subdivisions;
        out_norms.reserve(side_verts * 6);
        for (const auto& pos : out_pos) {
            out_norms.push_back(pos);
        }
    }

    inline void generate_cube_uvs(std::vector<Vector2>& out_uvs, int subdivisions) {
        size_t side_verts = 6 * subdivisions * subdivisions;
        out_uvs.reserve(side_verts * 6);
        // Generate side 0
        std::vector<Vector3> side_pos;
        side_pos.reserve(side_verts);
        generate_side(subdivisions, side_pos);
        for (const auto& v : side_pos) {
            out_uvs.emplace_back(v.x + 0.5, v.y + 0.5);
        }
        // Generate others
        for (int side = 1; side < 6; side++) {
            for (size_t i = 0; i < side_verts; i++) {
                out_uvs.push_back(out_uvs[i]);
            }
        }
    }

    inline void generate_sphere_uvs(
        std::ranges::input_range auto&& in_vertex_pos,
        std::vector<Vector2>& out_uvs
    ) {
        using std::numbers::pi;
        out_uvs.reserve(std::size(in_vertex_pos));
        for (const auto& v : in_vertex_pos) {
            out_uvs.emplace_back(
                std::atan2(v.y, v.x) / pi * 0.5 + 0.5,
                std::asin(v.z) / pi + 0.5
            );
        }
    }

    inline void generate_TB(
        std::ranges::input_range auto&& vertex_pos,
        std::ranges::input_range auto&& vertex_uv,
        std::vector<Vector3>* vertex_tangent,          // May be null
        std::vector<Vector3>* vertex_bitangent         // May be null
    ) {
        if (vertex_tangent == nullptr && vertex_bitangent == nullptr) { return; }
        if (vertex_tangent != nullptr) { vertex_tangent->reserve(std::size(vertex_pos)); }
        if (vertex_bitangent != nullptr) { vertex_bitangent->reserve(std::size(vertex_pos)); }
        size_t nVertices = std::size(vertex_pos);
        if (nVertices % 3) { throw std::runtime_error("nVertices % 3 != 0"); }
        for (size_t i = 0; i < nVertices; i += 3) {
            const std::span p = std::span(vertex_pos).subspan(i, 3);
            const std::span uv = std::span(vertex_uv).subspan(i, 3);
            const Vector3 e1 = p[1] - p[0];
            const Vector3 e2 = p[2] - p[0];
            const Vector2 duv1 = uv[1]-uv[0];
            const Vector2 duv2 = uv[2]-uv[0];
            const MatrixS<3, 2> dw ({
                e1.x, e2.x,
                e1.y, e2.y,
                e1.z, e2.z,
            });
            const MatrixS<2, 2> duv ({
                duv1.x, duv2.x,
                duv1.y, duv2.y,
            });
            const MatrixS<3, 2> TB = dw * duv.Inverse();

            for (size_t i = 0; i < 3; i++) {
                if (vertex_tangent != nullptr) {
                    vertex_tangent->emplace_back(
                        TB(0, 0),
                        TB(1, 0),
                        TB(2, 0)
                    );
                }
                if (vertex_bitangent != nullptr) {
                    vertex_bitangent->emplace_back(
                        TB(0, 1),
                        TB(1, 1),
                        TB(2, 1)
                    );
                }
            }
        }
    }

}

namespace primitives {

    struct mesh {
        std::vector<Vector3> pos;
        std::vector<Vector3> normals;
        std::vector<Vector2> uvs;
        std::vector<Vector3> tangents;
        std::vector<Vector3> bitangents;
    };

    inline mesh generate_cube(int subdivisions) {
        mesh ret;
        detail::generate_cube_pos(ret.pos, subdivisions);
        detail::generate_cube_normals(ret.normals, subdivisions);
        detail::generate_cube_uvs(ret.uvs, subdivisions);
        detail::generate_TB(ret.pos, ret.uvs, &ret.tangents, &ret.bitangents);
        return ret;
    }

    inline mesh generate_sphere(int subdivisions) {
        mesh ret;
        detail::generate_cube_pos(ret.pos, subdivisions);
        for (auto& v : ret.pos) {
            v.Normalize();
        }
        detail::generate_sphere_normals(ret.pos, ret.normals, subdivisions);
        detail::generate_sphere_uvs(ret.pos, ret.uvs);
        detail::generate_TB(ret.pos, ret.uvs, &ret.tangents, &ret.bitangents);
        return ret;
    }

    constexpr std::array<Vector2, 6> screenspace_quad = {
        Vector2(-1, -1),
        Vector2(+1, -1),
        Vector2(-1, +1),
        Vector2(-1, +1),
        Vector2(+1, -1),
        Vector2(+1, +1),
    };

}
