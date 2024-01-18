#pragma once
#include "GraphicsContext.h"

// Descriptor pool tied to a DescriptorSetLayout
struct TypedDescriptorPool {
    const GraphicsContext* vlk;
    vk::UniqueDescriptorSetLayout descriptorSetLayout;
    std::vector<vk::DescriptorPoolSize> poolSizes;
    std::vector<vk::UniqueDescriptorPool> descriptorPools;
public:
    vk::DescriptorSet alloc() const {
        return std::move(vlk->device->allocateDescriptorSets({
            .descriptorPool = descriptorPools.back().get(),
            .descriptorSetCount = 1,
            .pSetLayouts = &descriptorSetLayout.get(),
        })[0]);
    }
};

inline auto makeTypedDescriptorPool(
    const GraphicsContext* vlk,
    std::span<const vk::DescriptorSetLayoutBinding> bindings,
    size_t count
) {
    constexpr auto genPoolSizes = [](std::span<const vk::DescriptorSetLayoutBinding> bindings) {
        std::map<vk::DescriptorType, uint32_t> poolSizes;
        for (const auto& e : bindings) {
            poolSizes[e.descriptorType]++;
        }
        std::vector<vk::DescriptorPoolSize> ret;
        ret.reserve(poolSizes.size());
        for (const auto& [type, count] : poolSizes) {
            ret.push_back({
                .type = type,
                .descriptorCount = count,
            });
        }
        return ret;
    };
    TypedDescriptorPool ret = {
        .vlk = vlk,
        .descriptorSetLayout = vlk->createDescriptorSetLayout(bindings),
        .poolSizes = genPoolSizes(bindings),
        .descriptorPools = {},
    };
    ret.descriptorPools.push_back(vlk->createDescriptorPool(ret.poolSizes, count));
    return ret;
}
