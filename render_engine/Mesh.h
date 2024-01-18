#pragma once
#include "vlk/GraphicsContext.h"
#include "vlk/AssetPool.h"
#include "load_obj.h"

struct Mesh {
    vk::Buffer vertexBuffer;
    vk::Buffer indexBuffer;
    size_t nVertices;
    size_t nIndices;
    bool indexed;
};

inline Mesh makeMesh(const GraphicsContext* vlk, AssetPool& assets, std::string_view path) {
    const auto [vertices, indices] = load_obj(path);
    const auto vertexBuffer = std::get<vk::Buffer>(assets.storeTuple(vlk->createDeviceLocalBuffer(vk::BufferUsageFlagBits::eVertexBuffer, vertices)));
    const auto indexBuffer = std::get<vk::Buffer>(assets.storeTuple(vlk->createDeviceLocalBuffer(vk::BufferUsageFlagBits::eIndexBuffer, indices)));
    return Mesh {
        .vertexBuffer = vertexBuffer,
        .indexBuffer = indexBuffer,
        .nVertices = vertices.size(),
        .nIndices = indices.size(),
        .indexed = true,
    };
}
