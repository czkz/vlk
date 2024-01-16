#pragma once
#include "GraphicsContext.h"
#include "load_image.h"

// TODO should be TextureResources
struct Texture : public ImageAttachment {
    vk::UniqueSampler sampler;
};

inline auto makeTexture(GraphicsContext* vlk, std::string_view path, vk::Format format) {
    const int channels = std::map<vk::Format, int> {
        {vk::Format::eR8Srgb,       1},
        {vk::Format::eR8G8Srgb,     2},
        {vk::Format::eR8G8B8Srgb,   3},
        {vk::Format::eR8G8B8A8Srgb, 4},
    }.at(format);
    const auto img = load_image(path, channels);
    Texture ret;
    const uint32_t mipLevels = floor(log2(std::max(img.w, img.h))) + 1;
    std::tie(ret.image, ret.deviceMemory) = vlk->createDeviceLocalImageUnique(img, img.w, img.h, format, mipLevels);
    ret.imageView = vlk->device->createImageViewUnique({
        .flags = {},
        .image = ret.image.get(),
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .components = {},
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = mipLevels,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    });
    ret.sampler = vlk->device->createSamplerUnique({
        .flags = {},
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .mipLodBias = 0,
        .anisotropyEnable = vlk->props.maxAnisotropy != 0.0,
        .maxAnisotropy = vlk->props.maxAnisotropy,
        .compareEnable = VK_FALSE,
        .compareOp = {},
        .minLod = 0,
        .maxLod = vk::LodClampNone,
        .borderColor = {},
        .unnormalizedCoordinates = false,
    });
    return ret;
}
