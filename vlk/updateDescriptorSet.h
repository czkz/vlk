#pragma once
#include "GraphicsContext.h"
#include "Texture.h"

inline void updateTexture(
    GraphicsContext& vlk,
    vk::DescriptorSet descriptorSet,
    vk::DescriptorSetLayoutBinding layoutBinding,
    const Texture& texture
) {
    vlk.device->updateDescriptorSets({
        vk::WriteDescriptorSet {
            .dstSet = descriptorSet,
            .dstBinding = layoutBinding.binding,
            .dstArrayElement = 0,
            .descriptorCount = layoutBinding.descriptorCount,
            .descriptorType = layoutBinding.descriptorType,
            .pImageInfo = std::to_array({vk::DescriptorImageInfo {
                .sampler = texture.sampler.get(),
                .imageView = texture.imageView.get(),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            }}).data(),
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        },
    }, nullptr);
}

template <typename MaterialType, size_t I, typename T>
void updateDescriptor(
    GraphicsContext& vlk,
    vk::DescriptorSet descriptorSet,
    T&& resource
) {
    constexpr auto& a = MaterialType::descriptorSetLayoutBindings;
    if constexpr(a[I].descriptorType == vk::DescriptorType::eCombinedImageSampler) {
        updateTexture(vlk, descriptorSet, a[I], std::forward<T>(resource));
    } else {
        static_assert(!std::same_as<MaterialType, MaterialType>, "Unsupported descriptorType in material");
    }
}

template <typename MaterialType, size_t I = 0>
constexpr auto updateDescriptorSet = []<typename T, typename... Ts>(
    GraphicsContext& vlk,
    vk::DescriptorSet descriptorSet,
    T&& arg0,
    Ts&&... args
) {
    updateDescriptor<MaterialType, I>(vlk, descriptorSet, std::forward<T>(arg0));
    if constexpr (I+1 < MaterialType::descriptorSetLayoutBindings.size()) {
        updateDescriptorSet<MaterialType, I+1>(std::forward<Ts>(args)...);
    } else {
        static_assert(sizeof...(Ts) == 0, "Extra arguments");
    }
};

