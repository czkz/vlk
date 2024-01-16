#pragma once
#include "GraphicsContext.h"

struct MappedBuffer {
    std::pair<vk::UniqueBuffer, vk::UniqueDeviceMemory> buffer;
    void* mapping;
};

inline auto makeMappedBuffer(const GraphicsContext* vlk, uint32_t size, vk::BufferUsageFlags usage) {
    MappedBuffer ret;
    ret.buffer = vlk->createBufferUnique(size, usage, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    ret.mapping = vlk->device->mapMemory(ret.buffer.second.get(), 0, size, {});
    return ret;
}
