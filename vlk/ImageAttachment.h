#pragma once
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>

struct ImageAttachment {
    vk::UniqueImage image;
    vk::UniqueDeviceMemory deviceMemory;
    vk::UniqueImageView imageView;
};
