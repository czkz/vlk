#pragma once
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <memory>
#include <set>
#include <map>
#include <fstream>
#include <bit>
#include <ex.h>

struct GraphicsContext {
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window = {nullptr, glfwDestroyWindow};
    vk::UniqueInstance instance;
    vk::UniqueSurfaceKHR surface;
    vk::PhysicalDevice physicalDevice;
    struct Properties {
        vk::PhysicalDeviceFeatures deviceFeatures;
        vk::PhysicalDeviceProperties deviceProperties;
        float maxAnisotropy;
        vk::SampleCountFlagBits maxSampleCount;
        std::vector<vk::QueueFamilyProperties> queueFamilyProperties;
        uint32_t graphicsQueueFamily;
        uint32_t presentQueueFamily;
        std::set<uint32_t> uniqueQueueFamilies;
        vk::MemoryPropertyFlags memoryProperties;
    } props;
    vk::UniqueDevice device;
    vk::Queue graphicsQueue;
    vk::Queue presentQueue;
    vk::UniqueCommandPool commandPoolUtil;

    struct SwapchainResources {
        vk::SwapchainCreateInfoKHR         info;
        vk::UniqueSwapchainKHR             swapchain;
        std::vector<vk::UniqueImageView>   imageViews;
    } mutable swapchain;

    vk::UniqueCommandPool frameCommandPool;

    struct FrameInFlight {
        vk::UniqueCommandBuffer commandBuffer;
        vk::UniqueSemaphore imageAvailableSemaphore;
        vk::UniqueSemaphore renderFinishedSemaphore;
        vk::UniqueFence inFlightFence;
    };
    static constexpr uint32_t maxFramesInFlight = 1;
    std::array<FrameInFlight, maxFramesInFlight> framesInFlight;


    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags requiredProperties) const {
        const auto memoryProperties = physicalDevice.getMemoryProperties();
        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
            if (
                (typeFilter & (1 << i)) &&
                (memoryProperties.memoryTypes[i].propertyFlags & requiredProperties) == requiredProperties
            ) {
                return i;
            }
        }
        throw ex::runtime("couldn't find suitable memory type");
    }
    auto createBuffer(
        size_t nBytes,
        vk::BufferUsageFlags usage,
        vk::MemoryPropertyFlags properties
    ) const {
        auto buffer = device->createBufferUnique({
            .flags = {},
            .size = nBytes,
            .usage = usage,
            .sharingMode = vk::SharingMode::eExclusive,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
        });
        auto memoryRequirements = device->getBufferMemoryRequirements(buffer.get());
        auto memory = device->allocateMemoryUnique({
            .allocationSize = memoryRequirements.size,
            .memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, properties),
        });
        device->bindBufferMemory(buffer.get(), memory.get(), 0);
        return std::pair(std::move(buffer), std::move(memory));
    }
    auto createImage(
        const vk::ImageCreateInfo& createInfo,
        vk::MemoryPropertyFlags memoryProperties
    ) const {
        auto image = device->createImageUnique(createInfo);
        auto memoryRequirements = device->getImageMemoryRequirements(image.get());
        auto memory = device->allocateMemoryUnique({
            .allocationSize = memoryRequirements.size,
            .memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, memoryProperties),
        });
        device->bindImageMemory(image.get(), memory.get(), 0);
        return std::pair(std::move(image), std::move(memory));
    }
    void fillBuffer(const vk::UniqueDeviceMemory& memory, const auto& data) const {
        std::span bytes = data;
        void* mapped = device->mapMemory(memory.get(), 0, bytes.size_bytes(), {});
        memcpy(mapped, bytes.data(), bytes.size_bytes());
        device->unmapMemory(memory.get());
    }
    auto createStagingBuffer(const auto& data) const {
        auto ret = createBuffer(
            std::span(data).size_bytes(),
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        fillBuffer(ret.second, data);
        return ret;
    }
    auto tempCommandBuffer() const {
        class TempCommandBuffer {
            vk::UniqueCommandBuffer commandBuffer;
            vk::Queue queue;
            bool done = false;
            TempCommandBuffer(vk::UniqueCommandBuffer commandBuffer, vk::Queue queue)
                : commandBuffer(std::move(commandBuffer)), queue(std::move(queue)) {}
        public:
            TempCommandBuffer(TempCommandBuffer&& o)
                : commandBuffer(std::move(o.commandBuffer)), queue(std::move(o.queue)) { o.done = true; }
            operator const vk::UniqueCommandBuffer&() const { return commandBuffer; }
            const vk::UniqueCommandBuffer& operator->() const { return commandBuffer; }
            void end() {
                if (done) { return; }
                done = true;
                commandBuffer->end();
                queue.submit(vk::SubmitInfo {
                    .waitSemaphoreCount = 0,
                    .pWaitSemaphores = nullptr,
                    .pWaitDstStageMask = nullptr,
                    .commandBufferCount = 1,
                    .pCommandBuffers = &commandBuffer.get(),
                    .signalSemaphoreCount = 0,
                    .pSignalSemaphores = nullptr,
                }, nullptr);
                queue.waitIdle();
            }
            ~TempCommandBuffer() { end(); }
            static TempCommandBuffer make(vk::UniqueCommandBuffer commandBuffer, vk::Queue queue) {
                return TempCommandBuffer{std::move(commandBuffer), std::move(queue)};
            }
        };
        auto commandBuffer = std::move(device->allocateCommandBuffersUnique({
            .commandPool = commandPoolUtil.get(),
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1,
        })[0]);
        commandBuffer->begin({
            .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
            .pInheritanceInfo = 0,
        });
        return TempCommandBuffer::make(std::move(commandBuffer), graphicsQueue);
    }
    static auto cmdCopyBuffer(
        const vk::UniqueCommandBuffer& commandBuffer,
        const vk::UniqueBuffer& src,
        const vk::UniqueBuffer& dst,
        const vk::DeviceSize& size
    ) {
        commandBuffer->copyBuffer(src.get(), dst.get(), vk::BufferCopy {
            .srcOffset = 0,
            .dstOffset = 0,
            .size = size,
        });
    }
    static auto cmdCopyBufferToImage(
        const vk::UniqueCommandBuffer& commandBuffer,
        const vk::UniqueBuffer& src,
        const vk::UniqueImage& dst,
        size_t w,
        size_t h
    ) {
        commandBuffer->copyBufferToImage(src.get(), dst.get(), vk::ImageLayout::eTransferDstOptimal, vk::BufferImageCopy {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {
                .width = (uint32_t) w,
                .height = (uint32_t) h,
                .depth = 1,
            },
        });
    }
    auto createDeviceLocalBuffer(vk::BufferUsageFlags usage, const auto& data) const {
        const auto bytes = std::as_bytes(std::span(data));
        auto stagingBuffer = createStagingBuffer(bytes);
        auto localBuffer = createBuffer(
            bytes.size(),
            vk::BufferUsageFlagBits::eTransferDst | usage,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        cmdCopyBuffer(tempCommandBuffer(), stagingBuffer.first, localBuffer.first, bytes.size());
        return localBuffer;
    }
    auto createDeviceLocalImage(
        const auto& imageData,
        size_t w,
        size_t h,
        const vk::Format& format,
        uint32_t mipLevels
    ) const {
        const auto stagingBuffer = createStagingBuffer(imageData);
        using usage = vk::ImageUsageFlagBits;
        auto localImage = createImage({
            .flags = {},
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = vk::Extent3D {
                .width = (uint32_t) w,
                .height = (uint32_t) h,
                .depth = 1,
            },
            .mipLevels = mipLevels,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = usage::eTransferDst | usage::eTransferSrc | usage::eSampled,
            .sharingMode = vk::SharingMode::eExclusive,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr, // For shared sharingMode
            .initialLayout = vk::ImageLayout::eUndefined,
        }, vk::MemoryPropertyFlagBits::eDeviceLocal);
        auto commandBuffer = tempCommandBuffer();
        // Transition all mipmaps to eTransferDstOptimal
        commandBuffer->pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer,
            {},
            nullptr,
            nullptr,
            vk::ImageMemoryBarrier {
                .srcAccessMask = vk::AccessFlagBits::eNone,
                .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eTransferDstOptimal,
                .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                .image = localImage.first.get(),
                .subresourceRange = {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = mipLevels,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            }
        );
        cmdCopyBufferToImage(commandBuffer, stagingBuffer.first, localImage.first, w, h);
        // Generate mipmaps and transition
        [&] {
            const auto features = physicalDevice.getFormatProperties(format).optimalTilingFeatures;
            assert(features & vk::FormatFeatureFlagBits::eSampledImageFilterLinear);
            int32_t srcMipWidth = w;
            int32_t srcMipHeight = h;
            for (uint32_t i = 1; i < mipLevels; i++) {
                const int32_t dstMipWidth = std::max(srcMipWidth / 2, 1);
                const int32_t dstMipHeight = std::max(srcMipHeight / 2, 1);
                // Transition src mipmap to eTransferSrcOptimal
                commandBuffer->pipelineBarrier(
                    vk::PipelineStageFlagBits::eTransfer,
                    vk::PipelineStageFlagBits::eTransfer,
                    {},
                    nullptr,
                    nullptr,
                    vk::ImageMemoryBarrier {
                        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                        .dstAccessMask = vk::AccessFlagBits::eTransferRead,
                        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
                        .newLayout = vk::ImageLayout::eTransferSrcOptimal,
                        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                        .image = localImage.first.get(),
                        .subresourceRange = {
                            .aspectMask = vk::ImageAspectFlagBits::eColor,
                            .baseMipLevel = i - 1,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1,
                        },
                    }
                );
                // Generate dst mipmap
                commandBuffer->blitImage(
                    localImage.first.get(), vk::ImageLayout::eTransferSrcOptimal,
                    localImage.first.get(), vk::ImageLayout::eTransferDstOptimal,
                    vk::ImageBlit {
                        .srcSubresource = {
                            .aspectMask = vk::ImageAspectFlagBits::eColor,
                            .mipLevel = i - 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1,
                        },
                        .srcOffsets = std::to_array<vk::Offset3D>({
                            {0, 0, 0},
                            {srcMipWidth, srcMipHeight, 1},
                        }),
                        .dstSubresource = {
                            .aspectMask = vk::ImageAspectFlagBits::eColor,
                            .mipLevel = i,
                            .baseArrayLayer = 0,
                            .layerCount = 1,
                        },
                        .dstOffsets = std::to_array<vk::Offset3D>({
                            {0, 0, 0},
                            {dstMipWidth, dstMipHeight, 1},
                        }),
                    },
                    vk::Filter::eLinear
                );
                // Transition src mipmap to eShaderReadOnlyOptimal
                commandBuffer->pipelineBarrier(
                    vk::PipelineStageFlagBits::eTransfer,
                    vk::PipelineStageFlagBits::eFragmentShader,
                    {},
                    nullptr,
                    nullptr,
                    vk::ImageMemoryBarrier {
                        .srcAccessMask = vk::AccessFlagBits::eTransferRead,
                        .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                        .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
                        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                        .image = localImage.first.get(),
                        .subresourceRange = {
                            .aspectMask = vk::ImageAspectFlagBits::eColor,
                            .baseMipLevel = i - 1,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1,
                        },
                    }
                );
                srcMipWidth = dstMipWidth;
                srcMipHeight = dstMipHeight;
            }
            commandBuffer->pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eFragmentShader,
                {},
                nullptr,
                nullptr,
                vk::ImageMemoryBarrier {
                    .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                    .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                    .oldLayout = vk::ImageLayout::eTransferDstOptimal,
                    .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                    .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                    .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                    .image = localImage.first.get(),
                    .subresourceRange = {
                        .aspectMask = vk::ImageAspectFlagBits::eColor,
                        .baseMipLevel = mipLevels - 1,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                }
            );
        }();
        commandBuffer.end();
        return localImage;
    }
    auto createShaderModule(const char* filename) const {
        const auto slurp = [](const char* filename) {
            std::ifstream f(filename);
            return std::string(std::istreambuf_iterator<char>(f), {});
        };
        const auto code = slurp(filename);
        return device->createShaderModuleUnique({
            .flags = {},
            .codeSize = code.size(),
            .pCode = reinterpret_cast<const uint32_t*>(code.data()),
        });
    }
    auto genShaderStageCreateInfo(auto stage, const auto& module) const {
        vk::PipelineShaderStageCreateInfo createInfo = {
            .flags = {},
            .stage = stage,
            .module = module.get(),
            .pName = "main",
            .pSpecializationInfo = nullptr,
        };
        return createInfo;
    }
    auto createDescriptorSetLayout(std::span<const vk::DescriptorSetLayoutBinding> bindings) const {
        return device->createDescriptorSetLayoutUnique({
            .flags = {},
            .bindingCount = (uint32_t) bindings.size(),
            .pBindings = bindings.data(),
        });
    }
    vk::UniqueDescriptorPool createDescriptorPool(std::span<const vk::DescriptorPoolSize> poolSizes, size_t count) const {
        std::vector<vk::DescriptorPoolSize> poolSizesVec;
        if (count != 1) {
            poolSizesVec.reserve(poolSizes.size());
            std::ranges::copy(poolSizes, std::back_inserter(poolSizesVec));
            for (auto& e : poolSizesVec) {
                e.descriptorCount *= count;
            }
        }
        return device->createDescriptorPoolUnique({
            // .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .flags = {},
            .maxSets = (uint32_t) count,
            .poolSizeCount = (uint32_t) poolSizes.size(),
            .pPoolSizes = count == 1 ? poolSizes.data() : poolSizesVec.data(),
        });
    }

    void recreateSwapchain() const {
        swapchain.info = [&] {
            const auto caps = physicalDevice.getSurfaceCapabilitiesKHR(surface.get());
            const auto formats = physicalDevice.getSurfaceFormatsKHR(surface.get());
            const auto presentModes = physicalDevice.getSurfacePresentModesKHR(surface.get());
            vk::SurfaceFormatKHR bestFormat = [&] {
                for (const auto& f : formats) {
                    if (f.format == vk::Format::eB8G8R8A8Srgb && f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
                        return f;
                    }
                }
                const auto& ret = formats.front();
                prn_raw("Preferred swap chain format not available, using format ", ret.format, ", color space ", ret.colorSpace);
                return ret;
            }();
            vk::PresentModeKHR bestPresentMode = [&availableModes = presentModes] {
                // const auto preferredModes = std::to_array<vk::PresentModeKHR>({
                //     vk::PresentModeKHR::eMailbox,     // Triple-buffered VSync
                //     vk::PresentModeKHR::eFifoRelaxed, // VSync unless FPS < Refresh Rate
                //     vk::PresentModeKHR::eFifo,        // Always force Vsync
                //     vk::PresentModeKHR::eImmediate,   // No VSync
                // });
                const auto preferredModes = std::to_array<vk::PresentModeKHR>({vk::PresentModeKHR::eFifo});
                const auto modeNames = std::to_array({
                    "VK_PRESENT_MODE_IMMEDIATE_KHR",
                    "VK_PRESENT_MODE_MAILBOX_KHR",
                    "VK_PRESENT_MODE_FIFO_KHR",
                    "VK_PRESENT_MODE_FIFO_RELAXED_KHR",
                });
                const auto isAvailable = [&](auto mode) { return std::ranges::find(availableModes, mode) != std::end(availableModes); };
                const auto ret = std::ranges::find_if(preferredModes, isAvailable);
                assert(ret != std::end(preferredModes));
                prn("Using present mode", modeNames[static_cast<size_t>(*ret)]);
                return *ret;
            }();
            vk::Extent2D bestSwapExtent = [&] {
                if (caps.currentExtent.width != (uint32_t) -1) {
                    prn_raw("Using currentExtent: { ", caps.currentExtent.width, " ", caps.currentExtent.height, " }");
                    return caps.currentExtent;
                }
                int w, h;
                glfwGetFramebufferSize(window.get(), &w, &h);
                vk::Extent2D actualExtent = { (uint32_t) w, (uint32_t) h };
                vk::Extent2D clampedExtent = {
                    std::clamp(actualExtent.width, caps.minImageExtent.width, caps.maxImageExtent.width),
                    std::clamp(actualExtent.height, caps.minImageExtent.height, caps.maxImageExtent.height)
                };
                prn_raw("Actual extent: { ", actualExtent.width, " ", actualExtent.height, " }");
                if (
                    clampedExtent.width != actualExtent.width ||
                    clampedExtent.height != actualExtent.height
                ) {
                    prn_raw("Clamped extent differs from actual!");
                    prn_raw("Clamped extent: { ", clampedExtent.width, " ", clampedExtent.height, " }");
                }
                return clampedExtent;
            }();
            uint32_t bestImageCount = caps.minImageCount + 1;
            if (caps.maxImageCount != 0) {
                bestImageCount = std::min(bestImageCount, caps.maxImageCount);
            }
            prn_raw("Swap chain image count: ", bestImageCount, " (min ", caps.minImageCount, ", max ", caps.maxImageCount, ")");

            std::vector<uint32_t> uniqueQueueFamiliesVec;
            for (const auto& e : props.uniqueQueueFamilies) { uniqueQueueFamiliesVec.push_back(e); }

            return vk::SwapchainCreateInfoKHR {
                .flags = {},
                .surface = surface.get(),
                .minImageCount = bestImageCount,
                .imageFormat = bestFormat.format,
                .imageColorSpace = bestFormat.colorSpace,
                .imageExtent = bestSwapExtent,
                .imageArrayLayers = 1,
                .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
                .imageSharingMode = props.uniqueQueueFamilies.size() > 1 ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive,
                .queueFamilyIndexCount = (uint32_t) uniqueQueueFamiliesVec.size(),
                .pQueueFamilyIndices = uniqueQueueFamiliesVec.data(),
                .preTransform = caps.currentTransform,
                .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
                .presentMode = bestPresentMode,
                .clipped = VK_TRUE,
                .oldSwapchain = swapchain.swapchain.get(),
            };
        }();
        swapchain.swapchain = device->createSwapchainKHRUnique(swapchain.info);
        swapchain.imageViews = [&] {
            std::vector<vk::UniqueImageView> ret;
            for (const auto& image : device->getSwapchainImagesKHR(swapchain.swapchain.get())) {
                ret.push_back(device->createImageViewUnique({
                    .flags = {},
                    .image = image,
                    .viewType = vk::ImageViewType::e2D,
                    .format = swapchain.info.imageFormat,
                    .components = {},
                    .subresourceRange = {
                        .aspectMask = vk::ImageAspectFlagBits::eColor,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                }));
            }
            return ret;
        }();
    }
};

inline auto makeGraphicsContext() {
    glfwInit();
    GraphicsContext vlk;

    // Create window
    vlk.window = [w = 800, h = 600] {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        // glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        auto ptr = glfwCreateWindow(w, h, "Vulkan", nullptr, nullptr);
        return std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)>(ptr, glfwDestroyWindow);
    }();

    // Create Vulkan instance
    vlk.instance = [] {
        // List of required instance layers
        const auto requiredInstanceLayers = std::to_array({"VK_LAYER_KHRONOS_validation"});
        // const std::array<const char*, 0> requiredInstanceLayers;

        // Check layer support
        [&requiredInstanceLayers] {
            const auto availableInstanceLayers = vk::enumerateInstanceLayerProperties();

            prn("Available layers:");
            for (const auto& e : availableInstanceLayers) { prn('\t', std::string_view(e.layerName)); }

            for (const auto& name : requiredInstanceLayers) {
                if (!std::ranges::any_of(availableInstanceLayers, [name](const auto& e) { return std::string_view(e.layerName) == name; })) {
                    throw ex::runtime(fmt("layer", name, "not available"));
                }
            }
        }();

        // Generate a list of required instance extensions
        const auto glfwInstanceExtensions = [] {
            uint32_t glfwExtensionCount = 0;
            const char** glfwExtensionsRaw;
            glfwExtensionsRaw = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
            return std::span<const char* const>(glfwExtensionsRaw, glfwExtensionCount);
        }();
        const auto requiredInstanceExtensions = glfwInstanceExtensions;

        // Check extension support
        [&requiredInstanceExtensions] {
            const auto availableInstanceExtensions = vk::enumerateInstanceExtensionProperties();

            prn("Available extensions:");
            for (const auto& e : availableInstanceExtensions) { prn('\t', std::string_view(e.extensionName)); }

            for (const auto& name : requiredInstanceExtensions) {
                if (!std::ranges::any_of(availableInstanceExtensions, [name](const auto& e) { return std::string_view(e.extensionName) == name; })) {
                    throw ex::runtime(fmt("extension", name, "not available"));
                }
            }
        }();

        // Create instance
        return [&requiredInstanceLayers, &requiredInstanceExtensions] {
            const vk::ApplicationInfo appInfo = {
                .pApplicationName = "Vulkan",
                .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
                .pEngineName = "No Engine",
                .engineVersion = VK_MAKE_VERSION(1, 0, 0),
                .apiVersion = VK_API_VERSION_1_0,
            };
            return vk::createInstanceUnique({
                .flags = {},
                .pApplicationInfo = &appInfo,
                .enabledLayerCount = (uint32_t) requiredInstanceLayers.size(),
                .ppEnabledLayerNames = requiredInstanceLayers.data(),
                .enabledExtensionCount = (uint32_t) requiredInstanceExtensions.size(),
                .ppEnabledExtensionNames = requiredInstanceExtensions.data(),
            });
        }();
    }();

    // Create window surface
    vlk.surface = [&] {
        VkSurfaceKHR ret;
        exwrap(glfwCreateWindowSurface(vlk.instance.get(), vlk.window.get(), nullptr, &ret));
        return vk::UniqueSurfaceKHR(ret, vlk.instance.get());
    }();

    // List of required device extensions
    constexpr auto requiredDeviceExtensions = std::to_array({
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    });

    // Pick physical device
    vlk.physicalDevice = [&] {
        // Get a list of available devices
        const auto availableDevices = vlk.instance->enumeratePhysicalDevices();
        if (availableDevices.size() == 0) {
            throw ex::runtime("couldn't find devices with Vulkan support");
        }

        // Print available devices
        [&availableDevices] {
            prn("Available physical devices:");
            for (const auto& device : availableDevices) {
                const auto properties = device.getProperties();
                const auto features = device.getFeatures();
                constexpr auto types = std::to_array({
                    "Other",
                    "Integrated GPU",
                    "Discrete GPU",
                    "Virtual GPU",
                    "CPU",
                });
                prn("\t", std::string_view(properties.deviceName));
                prn("\t\t", "Type:", types[static_cast<size_t>(properties.deviceType)]);
                prn("\t\t", "API:", properties.apiVersion);
                prn("\t\t", "Framebuffer dimensions:", properties.limits.maxFramebufferWidth, "x", properties.limits.maxFramebufferHeight);
                prn("\t\t", "Geometry shader supported:", (bool) features.geometryShader);
                prn("\t\t", "Tesselation shader supported:", (bool) features.tessellationShader);
            }
        }();

        // Get a list of suitable devices
        const auto isSuitable = [&](const vk::PhysicalDevice& device) {
            const bool deviceSupportsExtensions = [&device, &extensions = requiredDeviceExtensions] {
                const auto availableExtensions = device.enumerateDeviceExtensionProperties();
                for (const auto& name : extensions) {
                    if (!std::ranges::any_of(availableExtensions, [name](const auto& e) { return std::string_view(e.extensionName) == name; })) {
                        return false;
                    }
                }
                return true;
            }();
            const auto queueFamilies = device.getQueueFamilyProperties();
            const bool hasGraphicsQueueFamily = [&] {
                for (uint32_t i = 0; i < queueFamilies.size(); i++) {
                    if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                        return true;
                    }
                }
                return false;
            }();
            const bool hasPresentQueueFamily = [&] {
                for (uint32_t i = 0; i < queueFamilies.size(); i++) {
                    if (device.getSurfaceSupportKHR(i, vlk.surface.get())) {
                        return true;
                    }
                }
                return false;
            }();
            return
                deviceSupportsExtensions &&
                hasGraphicsQueueFamily &&
                hasPresentQueueFamily &&
                device.getSurfaceFormatsKHR(vlk.surface.get()).size() > 0 &&
                device.getSurfacePresentModesKHR(vlk.surface.get()).size() > 0;
        };

        const auto suitableDevices = [&availableDevices, &isSuitable] {
            std::vector<vk::PhysicalDevice> ret;
            std::ranges::copy_if(availableDevices, std::back_inserter(ret), isSuitable);
            return ret;
        }();
        if (suitableDevices.empty()) {
            throw ex::runtime("couldn't find a suitable device");
        }

        prn("Suitable physical devices:");
        for (const auto& device : suitableDevices) {
            prn("\t", std::string_view(device.getProperties().deviceName));
        }

        // Pick best
        return [&suitableDevices] {
            const auto compare = [](const vk::PhysicalDevice& a, const vk::PhysicalDevice& b) {
                using t = vk::PhysicalDeviceType;
                std::map<t, int> typeToPriority {
                    {t::eDiscreteGpu,   0},
                    {t::eOther,         1},
                    {t::eIntegratedGpu, 2},
                    {t::eVirtualGpu,    3},
                };
                int p0 = typeToPriority[a.getProperties().deviceType];
                int p1 = typeToPriority[b.getProperties().deviceType];
                auto m0 = a.getMemoryProperties().memoryHeaps.at(0).size;
                auto m1 = b.getMemoryProperties().memoryHeaps.at(0).size;
                if (p0 != p1) { return p0 < p1; }
                return m0 >= m1;
            };
            return *std::ranges::min_element(suitableDevices, compare);
        }();
    }();
    prn("Chosen physical device:", std::string_view(vlk.physicalDevice.getProperties().deviceName));

    // Get some device properties and limits
    vlk.props.deviceFeatures = vlk.physicalDevice.getFeatures();
    vlk.props.deviceProperties = vlk.physicalDevice.getProperties();
    vlk.props.maxAnisotropy = vlk.props.deviceFeatures.samplerAnisotropy ? vlk.props.deviceProperties.limits.maxSamplerAnisotropy : 0;
    vlk.props.maxSampleCount = [&limits = vlk.props.deviceProperties.limits] {
        const auto i = static_cast<uint32_t>(limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts);
        return static_cast<vk::SampleCountFlagBits>(std::bit_floor(i));
    }();
    vlk.props.queueFamilyProperties = vlk.physicalDevice.getQueueFamilyProperties();
    prn("Anisotropic filtering:", vlk.props.maxAnisotropy ? fmt_raw(static_cast<uint32_t>(vlk.props.maxAnisotropy), "x") : "disabled");
    prn("Multisampling:", fmt_raw(static_cast<uint32_t>(vlk.props.maxSampleCount), "x"));


    // Get queue family indices for chosen device
    vlk.props.graphicsQueueFamily = [&] {
        for (uint32_t i = 0; i < vlk.props.queueFamilyProperties.size(); i++) {
            if (!(vlk.props.queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics)) { continue; }
            return i;
        }
        assert(false);
    }();
    vlk.props.presentQueueFamily = [&] {
        if (vlk.physicalDevice.getSurfaceSupportKHR(vlk.props.graphicsQueueFamily, vlk.surface.get())) {
            return vlk.props.graphicsQueueFamily;
        }
        const auto len = vlk.physicalDevice.getQueueFamilyProperties().size();
        for (uint32_t i = 0; i < len; i++) {
            if (vlk.physicalDevice.getSurfaceSupportKHR(i, vlk.surface.get())) {
                return i;
            }
        }
        assert(false);
    }();
    vlk.props.uniqueQueueFamilies = {vlk.props.graphicsQueueFamily, vlk.props.presentQueueFamily};

    // Create logical device
    vlk.device = [&] {
        const float queuePriority = 1.0f;
        std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
        for (const auto& e : vlk.props.uniqueQueueFamilies) {
            queueCreateInfos.push_back({
                .flags = {},
                .queueFamilyIndex = e,
                .queueCount = 1,
                .pQueuePriorities = &queuePriority,
            });
        }
        const vk::PhysicalDeviceFeatures usedFeatures {
            .samplerAnisotropy = vlk.props.deviceFeatures.samplerAnisotropy,
        };
        return vlk.physicalDevice.createDeviceUnique({
            .flags = {},
            .queueCreateInfoCount = (uint32_t) queueCreateInfos.size(),
            .pQueueCreateInfos = queueCreateInfos.data(),
            .enabledExtensionCount = (uint32_t) requiredDeviceExtensions.size(), // Device extensions, not instance extensions
            .ppEnabledExtensionNames = requiredDeviceExtensions.data(),
            .pEnabledFeatures = &usedFeatures,
        });
    }();

    vlk.graphicsQueue = vlk.device->getQueue(vlk.props.graphicsQueueFamily, 0);
    vlk.presentQueue = vlk.device->getQueue(vlk.props.presentQueueFamily, 0);

    vlk.commandPoolUtil = vlk.device->createCommandPoolUnique({
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = vlk.props.graphicsQueueFamily,
    });

    vlk.recreateSwapchain();

    vlk.frameCommandPool = vlk.device->createCommandPoolUnique({
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = vlk.props.graphicsQueueFamily,
    });

    for (auto& f : vlk.framesInFlight) {
        f = {
            .commandBuffer = std::move(vlk.device->allocateCommandBuffersUnique({
                .commandPool = vlk.frameCommandPool.get(),
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = 1,
            })[0]),
            .imageAvailableSemaphore = vlk.device->createSemaphoreUnique({}),
            .renderFinishedSemaphore = vlk.device->createSemaphoreUnique({}),
            .inFlightFence = vlk.device->createFenceUnique({
                .flags = vk::FenceCreateFlagBits::eSignaled,
            }),
        };
    }

    return vlk;
}
