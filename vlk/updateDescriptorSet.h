#pragma once
#include "GraphicsContext.h"
#include "Texture.h"

inline void updateTexture(
    const GraphicsContext* vlk,
    vk::DescriptorSet descriptorSet,
    vk::DescriptorSetLayoutBinding layoutBinding,
    const Texture& texture
) {
    vlk->device->updateDescriptorSets({
        vk::WriteDescriptorSet {
            .dstSet = descriptorSet,
            .dstBinding = layoutBinding.binding,
            .dstArrayElement = 0,
            .descriptorCount = layoutBinding.descriptorCount,
            .descriptorType = layoutBinding.descriptorType,
            .pImageInfo = std::to_array({vk::DescriptorImageInfo {
                .sampler = texture.sampler,
                .imageView = texture.imageView,
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            }}).data(),
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        },
    }, nullptr);
}

inline void updateDescriptorSet(
    const GraphicsContext* vlk,
    vk::DescriptorSet descriptorSet,
    std::span<const vk::DescriptorSetLayoutBinding> bindings,
    std::span<const Texture> textures
) {
    assert(bindings.size() == textures.size());
    for (size_t i = 0; i < bindings.size(); i++) {
        assert(bindings[i].descriptorType == vk::DescriptorType::eCombinedImageSampler);
        updateTexture(vlk, descriptorSet, bindings[i], textures[i]);
    }
};
