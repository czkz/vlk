#pragma once
#include "vlk/GraphicsContext.h"
#include "vlk/TypedDescriptorPool.h"
#include "updateDescriptorSet.h"
#include "Texture.h"

struct Material {
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;
};

struct MaterialType {
    TypedDescriptorPool descriptorPool;
    std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBindings;
    Material makeMaterial(std::span<const Texture> textures) const {
        auto descriptorSet = descriptorPool.alloc();
        updateDescriptorSet(descriptorPool.vlk, descriptorSet, descriptorSetLayoutBindings, textures);
        Material ret = {
            .descriptorSet = descriptorSet,
            .descriptorSetLayout = descriptorPool.descriptorSetLayout.get(),
        };
        return ret;
    }
};

inline auto makeMaterialType(const GraphicsContext* vlk, std::span<const vk::DescriptorSetLayoutBinding> bindings) {
    return MaterialType {
        .descriptorPool = makeTypedDescriptorPool(vlk, bindings, 1),
        .descriptorSetLayoutBindings = std::vector(bindings.begin(), bindings.end()),
    };
}
