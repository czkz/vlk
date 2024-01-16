#pragma once
#include "GraphicsContext.h"

inline vk::UniquePipelineLayout createPipelineLayout(
    GraphicsContext& vlk,
    std::span<const vk::DescriptorSetLayout> descriptorSetLayouts,
    std::span<const vk::PushConstantRange> pushConstantRanges
) {
    return vlk.device->createPipelineLayoutUnique({
        .flags = {},
        .setLayoutCount = (uint32_t) descriptorSetLayouts.size(),
        .pSetLayouts = descriptorSetLayouts.data(),
        .pushConstantRangeCount = (uint32_t) pushConstantRanges.size(),
        .pPushConstantRanges = pushConstantRanges.data(),
    });
}

