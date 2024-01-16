#pragma once
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>

class AssetPool {
    std::vector<vk::UniqueDeviceMemory> memory;
    std::vector<vk::UniqueBuffer> buffers;
    std::vector<vk::UniqueImage> images;
    std::vector<vk::UniqueImageView> imageViews;
    std::vector<vk::UniqueSampler> samplers;
    // std::vector<vk::UniqueDescriptorSet> descriptorSets;
private:
    auto storeImpl(auto v, auto& vec) {
        auto ret = v.get();
        vec.push_back(std::move(v));
        return ret;
    }
public:
    auto store(vk::UniqueDeviceMemory resource) { return storeImpl(std::move(resource), memory); }
    auto store(vk::UniqueBuffer resource) { return storeImpl(std::move(resource), buffers); }
    auto store(vk::UniqueImage resource) { return storeImpl(std::move(resource), images); }
    auto store(vk::UniqueImageView resource) { return storeImpl(std::move(resource), imageViews); }
    auto store(vk::UniqueSampler resource) { return storeImpl(std::move(resource), samplers); }
    auto storeTuple(auto&& resourcesTuple) {
        return std::apply([this](auto... args) { return std::make_tuple(this->store(std::move(args))...); }, std::forward<decltype(resourcesTuple)>(resourcesTuple));
    }
};

