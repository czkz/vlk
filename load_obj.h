#include "ex.h"
#include <unordered_map>
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#include <Vector.h>

inline auto load_obj(std::string_view path) {
    struct Vertex {
        Vector3 pos;
        Vector2 uv;
        constexpr bool operator==(const Vertex&) const = default;
    };
    std::pair<std::vector<Vertex>, std::vector<uint32_t>> ret;
    auto hash = [](const Vertex& v) {
        std::size_t seed = 5;
        for(auto f : {v.pos.x, v.pos.y, v.pos.z, v.uv.x, v.uv.y}) {
            static_assert(sizeof(uint32_t) == sizeof(f));
            uint32_t x;
            memcpy(&x, &f, sizeof(f));
            x = ((x >> 16) ^ x) * 0x45d9f3b;
            x = ((x >> 16) ^ x) * 0x45d9f3b;
            x = (x >> 16) ^ x;
            seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    };
    std::unordered_map<Vertex, int, decltype(hash)> vertex_map;
    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(std::string(path), {})) {
        throw ex::runtime(reader.Error());
    }
    // if (!reader.Warning().empty()) { prn("TinyObjReader:", reader.Warning()); }
    const auto& attrib = reader.GetAttrib();
    for (const auto& shape : reader.GetShapes()) {
        for (const auto& mesh_index : shape.mesh.indices) {
            const Vertex vertex = {
                .pos = {
                    attrib.vertices[3 * mesh_index.vertex_index + 0],
                    attrib.vertices[3 * mesh_index.vertex_index + 1],
                    attrib.vertices[3 * mesh_index.vertex_index + 2],
                },
                .uv = {
                    attrib.texcoords[2 * mesh_index.texcoord_index + 0],
                    1 - attrib.texcoords[2 * mesh_index.texcoord_index + 1],
                },
            };
            const auto iter = vertex_map.find(vertex);
            uint32_t index = -1;
            if (iter != vertex_map.end()) {
                index = iter->second;
            } else {
                index = vertex_map.size();
                vertex_map.emplace_hint(iter, vertex, index);
                ret.first.push_back(vertex);
            }
            ret.second.push_back(index);
        }
    }
    return ret;
}
