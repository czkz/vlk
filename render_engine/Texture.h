#pragma once
#include "vlk/GraphicsContext.h"
#include "vlk/AssetPool.h"
#include "load_image.h"

struct Texture {
    vk::Image image;
    vk::ImageView imageView;
    vk::Sampler sampler;
};

inline auto makeTexture(GraphicsContext* vlk, AssetPool& assets, std::string_view path, vk::Format format) {
    const int channels = std::map<vk::Format, int> {
        {vk::Format::eR8Srgb,       1},
        {vk::Format::eR8G8Srgb,     2},
        {vk::Format::eR8G8B8Srgb,   3},
        {vk::Format::eR8G8B8A8Srgb, 4},
    }.at(format);
    const auto img = load_image(path, channels);
    const uint32_t mipLevels = floor(log2(std::max(img.w, img.h))) + 1;
    const auto image = std::get<vk::Image>(assets.storeTuple(vlk->createDeviceLocalImageUnique(img, img.w, img.h, format, mipLevels)));
    const auto imageView = assets.store(vlk->device->createImageViewUnique({
        .flags = {},
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .components = {},
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = vk::RemainingMipLevels,
            .baseArrayLayer = 0,
            .layerCount = vk::RemainingArrayLayers,
        },
    }));
    const auto sampler = assets.store(vlk->device->createSamplerUnique({
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
    }));
    return Texture {
        .image = image,
        .imageView = imageView,
        .sampler = sampler,
    };
}
