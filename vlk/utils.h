#pragma once
#include "GraphicsContext.h"

inline vk::UniquePipelineLayout createPipelineLayout(
    const GraphicsContext* vlk,
    std::span<const vk::DescriptorSetLayout> descriptorSetLayouts,
    std::span<const vk::PushConstantRange> pushConstantRanges
) {
    return vlk->device->createPipelineLayoutUnique({
        .flags = {},
        .setLayoutCount = (uint32_t) descriptorSetLayouts.size(),
        .pSetLayouts = descriptorSetLayouts.data(),
        .pushConstantRangeCount = (uint32_t) pushConstantRanges.size(),
        .pPushConstantRanges = pushConstantRanges.data(),
    });
}

struct Frame {
    vk::CommandBuffer commandBuffer;
    uint32_t frameIndex;
    uint32_t imageIndex;
};

struct RenderTarget {
    vk::Extent2D extent;
    vk::Format format;
    std::span<const vk::ImageView> imageViews;
};
