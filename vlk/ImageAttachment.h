#pragma once
#include "vlk/GraphicsContext.h"

struct ImageAttachment {
    vk::UniqueImage image;
    vk::UniqueDeviceMemory deviceMemory;
    vk::UniqueImageView imageView;
};

inline auto makeImageAttachment(
    const GraphicsContext* vlk,
    const vk::ImageCreateInfo& createInfo,
    vk::MemoryPropertyFlags memoryProperties,
    vk::ImageAspectFlags aspectMask
) {
    auto [image, deviceMemory] = vlk->createImage(createInfo, memoryProperties);
    auto imageView = vlk->device->createImageViewUnique({
        .flags = {},
        .image = image.get(),
        .viewType = vk::ImageViewType::e2D,
        .format = createInfo.format,
        .components = {},
        .subresourceRange = {
            .aspectMask = aspectMask,
            .baseMipLevel = 0,
            .levelCount = vk::RemainingMipLevels,
            .baseArrayLayer = 0,
            .layerCount = vk::RemainingArrayLayers,
        },
    });
    return ImageAttachment {
        .image = std::move(image),
        .deviceMemory = std::move(deviceMemory),
        .imageView = std::move(imageView),
    };
}
