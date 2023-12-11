#include "Camera.h"
#include <cstddef>
#include <memory>
#include <set>
#include <fstream>
#include <bit>
#define VULKAN_HPP_NO_CONSTRUCTORS
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>
#include <fmt.h>
#include <ex.h>
#include <Vector.h>
#include <Stopwatch.h>
#include "primitives.h"
#include "FrameCounter.h"
#include "load_image.h"
#include "input.h"
#include "load_obj.h"

struct ImageAttachment {
    vk::UniqueImage image;
    vk::UniqueDeviceMemory deviceMemory;
    vk::UniqueImageView imageView;
};

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
        ImageAttachment                    colorAttachment;
        ImageAttachment                    depthAttachment;
        std::vector<vk::UniqueFramebuffer> framebuffers;
    } swapchain;

    vk::UniqueRenderPass renderPass;

    vk::UniqueCommandPool commandPool;
    vk::UniqueDescriptorPool descriptorPool;

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
    auto createBufferUnique(
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
    auto createImageUnique(
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
    auto createStagingBufferUnique(const auto& data) const {
        auto ret = createBufferUnique(
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
    auto createDeviceLocalBufferUnique(vk::BufferUsageFlags usage, const auto& data) const {
        const auto bytes = std::as_bytes(std::span(data));
        auto stagingBuffer = createStagingBufferUnique(bytes);
        auto localBuffer = createBufferUnique(
            bytes.size(),
            vk::BufferUsageFlagBits::eTransferDst | usage,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        cmdCopyBuffer(tempCommandBuffer(), stagingBuffer.first, localBuffer.first, bytes.size());
        return localBuffer;
    }
    auto createDeviceLocalImageUnique(
        const auto& imageData,
        size_t w,
        size_t h,
        const vk::Format& format,
        uint32_t mipLevels
    ) const {
        const auto stagingBuffer = createStagingBufferUnique(imageData);
        using usage = vk::ImageUsageFlagBits;
        auto localImage = createImageUnique({
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
    auto createImageAttachmentUnique(
        const vk::ImageCreateInfo& createInfo,
        vk::MemoryPropertyFlags memoryProperties,
        vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor
    ) const {
        ImageAttachment ret;
        std::tie(ret.image, ret.deviceMemory) = createImageUnique(createInfo, memoryProperties);
        ret.imageView = device->createImageViewUnique({
            .flags = {},
            .image = ret.image.get(),
            .viewType = vk::ImageViewType::e2D,
            .format = createInfo.format,
            .components = {},
            .subresourceRange = {
                .aspectMask = aspectMask,
                .baseMipLevel = 0,
                .levelCount = createInfo.mipLevels,
                .baseArrayLayer = 0,
                .layerCount = createInfo.arrayLayers,
            },
        });
        return ret;
    }
    auto createShaderModuleUnique(const char* filename) const {
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
    auto createDescriptorSetLayoutUnique(std::span<const vk::DescriptorSetLayoutBinding> bindings) const {
        return device->createDescriptorSetLayoutUnique({
            .flags = {},
            .bindingCount = (uint32_t) bindings.size(),
            .pBindings = bindings.data(),
        });
    }
    vk::UniqueDescriptorPool createDescriptorPoolUnique(std::span<const vk::DescriptorPoolSize> poolSizes, size_t count) const {
        std::vector<vk::DescriptorPoolSize> poolSizesVec;
        if (count != 1) {
            poolSizesVec.reserve(poolSizes.size());
            std::ranges::copy(poolSizes, std::back_inserter(poolSizesVec));
            for (auto& e : poolSizesVec) {
                e.descriptorCount *= count;
            }
        }
        return device->createDescriptorPoolUnique({
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = (uint32_t) count,
            .poolSizeCount = (uint32_t) poolSizes.size(),
            .pPoolSizes = count == 1 ? poolSizes.data() : poolSizesVec.data(),
        });
    }


    void recreateSwapchainUnique() {
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
        swapchain.colorAttachment = createImageAttachmentUnique({
            .flags = {},
            .imageType = vk::ImageType::e2D,
            .format = swapchain.info.imageFormat,
            .extent = {
                swapchain.info.imageExtent.width,
                swapchain.info.imageExtent.height,
                1
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = props.maxSampleCount,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eColorAttachment,
            .sharingMode = vk::SharingMode::eExclusive,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = vk::ImageLayout::eUndefined,
        }, vk::MemoryPropertyFlagBits::eDeviceLocal);
        swapchain.depthAttachment = createImageAttachmentUnique({
            .flags = {},
            .imageType = vk::ImageType::e2D,
            .format = vk::Format::eD32Sfloat,
            .extent = {
                .width = swapchain.info.imageExtent.width,
                .height = swapchain.info.imageExtent.height,
                .depth = 1,
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = props.maxSampleCount,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
            .sharingMode = vk::SharingMode::eExclusive,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr, // For shared sharingMode
            .initialLayout = vk::ImageLayout::eUndefined,
        }, vk::MemoryPropertyFlagBits::eDeviceLocal, vk::ImageAspectFlagBits::eDepth);
    }

    void recreateFramebuffersUnique() {
        swapchain.framebuffers.clear();
        for (const auto& resolveImageView : swapchain.imageViews) {
            const auto attachments = std::to_array({
                swapchain.colorAttachment.imageView.get(),
                swapchain.depthAttachment.imageView.get(),
                resolveImageView.get(),
            });
            swapchain.framebuffers.push_back(device->createFramebufferUnique({
                .flags = {},
                .renderPass = renderPass.get(),
                .attachmentCount = attachments.size(),
                .pAttachments = attachments.data(),
                .width = swapchain.info.imageExtent.width,
                .height = swapchain.info.imageExtent.height,
                .layers = 1,
            }));
        }
    }
};
GraphicsContext makeGraphicsContext() {
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

    vlk.recreateSwapchainUnique();

    vlk.renderPass = [&] {
        vk::AttachmentDescription colorAttachmentDesc = {
            .flags = {},
            .format = vlk.swapchain.info.imageFormat,
            .samples = vlk.props.maxSampleCount,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eDontCare,
            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
            .initialLayout = vk::ImageLayout::eUndefined,
            .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
        };
        vk::AttachmentDescription depthAttachmentDesc = {
            .flags = {},
            .format = vk::Format::eD32Sfloat,
            .samples = vlk.props.maxSampleCount,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eDontCare,
            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
            .initialLayout = vk::ImageLayout::eUndefined,
            .finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        };
        vk::AttachmentDescription colorResolveDesc = {
            .flags = {},
            .format = vlk.swapchain.info.imageFormat,
            .samples = vk::SampleCountFlagBits::e1,
            .loadOp = vk::AttachmentLoadOp::eDontCare,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
            .initialLayout = vk::ImageLayout::eUndefined,
            .finalLayout = vk::ImageLayout::ePresentSrcKHR,
        };
        vk::AttachmentReference colorAttachmentRef = {
            .attachment = 0,
            .layout = vk::ImageLayout::eColorAttachmentOptimal,
        };
        vk::AttachmentReference depthAttachmentRef = {
            .attachment = 1,
            .layout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        };
        vk::AttachmentReference colorResolveRef = {
            .attachment = 2,
            .layout = vk::ImageLayout::eColorAttachmentOptimal,
        };
        vk::SubpassDescription subpass = {
            .flags = {},
            .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
            .inputAttachmentCount = 0,
            .pInputAttachments = nullptr,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentRef,
            .pResolveAttachments = &colorResolveRef,
            .pDepthStencilAttachment = &depthAttachmentRef,
            .preserveAttachmentCount = 0,
            .pPreserveAttachments = nullptr,
        };
        using stage = vk::PipelineStageFlagBits;
        vk::SubpassDependency extenralDependency = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = stage::eColorAttachmentOutput | stage::eEarlyFragmentTests | stage::eLateFragmentTests,
            .dstStageMask = stage::eColorAttachmentOutput | stage::eEarlyFragmentTests | stage::eLateFragmentTests,
            .srcAccessMask = vk::AccessFlagBits::eNone,
            .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
            .dependencyFlags = {},
        };
        const auto attachmentDescriptions = std::to_array({colorAttachmentDesc, depthAttachmentDesc, colorResolveDesc});
        vk::RenderPassCreateInfo createInfo = {
            .flags = {},
            .attachmentCount = attachmentDescriptions.size(),
            .pAttachments = attachmentDescriptions.data(),
            .subpassCount = 1,
            .pSubpasses = &subpass,
            .dependencyCount = 1,
            .pDependencies = &extenralDependency,
        };
        return vlk.device->createRenderPassUnique(createInfo);
    }();

    vlk.recreateFramebuffersUnique();

    vlk.commandPool = vlk.device->createCommandPoolUnique({
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = vlk.props.graphicsQueueFamily,
    });

    vlk.descriptorPool = [&] {
        const auto poolSizes = std::to_array({
            vk::DescriptorPoolSize {
                .type = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = 2,
            },
            vk::DescriptorPoolSize {
                .type = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = 2,
            },
        });
        return vlk.device->createDescriptorPoolUnique({
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = 2,
            .poolSizeCount = poolSizes.size(),
            .pPoolSizes = poolSizes.data(),
        });
    }();

    for (auto& f : vlk.framesInFlight) {
        f = {
            .commandBuffer = std::move(vlk.device->allocateCommandBuffersUnique({
                .commandPool = vlk.commandPool.get(),
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

struct Mesh {
    std::pair<vk::UniqueBuffer, vk::UniqueDeviceMemory> vertexBuffer;
    std::pair<vk::UniqueBuffer, vk::UniqueDeviceMemory> indexBuffer;
    size_t nVertices;
    size_t nIndices;
    bool indexed;
};
Mesh makeMesh(const GraphicsContext& vlk, std::string_view path) {
    const auto [vertices, indices] = load_obj(path);
    return Mesh {
        .vertexBuffer = vlk.createDeviceLocalBufferUnique(vk::BufferUsageFlagBits::eVertexBuffer, vertices),
        .indexBuffer = vlk.createDeviceLocalBufferUnique(vk::BufferUsageFlagBits::eIndexBuffer, indices),
        .nVertices = vertices.size(),
        .nIndices = indices.size(),
        .indexed = true,
    };
}

struct Texture : public ImageAttachment {
    vk::UniqueSampler sampler;
};
Texture makeTexture(const GraphicsContext& vlk, std::string_view path, vk::Format format) {
    const int channels = std::map<vk::Format, int> {
        {vk::Format::eR8Srgb,       1},
        {vk::Format::eR8G8Srgb,     2},
        {vk::Format::eR8G8B8Srgb,   3},
        {vk::Format::eR8G8B8A8Srgb, 4},
    }[format];
    const auto img = load_image(path, channels);
    Texture ret;
    const uint32_t mipLevels = floor(log2(std::max(img.w, img.h))) + 1;
    std::tie(ret.image, ret.deviceMemory) = vlk.createDeviceLocalImageUnique(img, img.w, img.h, format, mipLevels);
    ret.imageView = vlk.device->createImageViewUnique({
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
    ret.sampler = vlk.device->createSamplerUnique({
        .flags = {},
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .mipLodBias = 0,
        .anisotropyEnable = vlk.props.maxAnisotropy != 0.0,
        .maxAnisotropy = vlk.props.maxAnisotropy,
        .compareEnable = VK_FALSE,
        .compareOp = {},
        .minLod = 0,
        .maxLod = vk::LodClampNone,
        .borderColor = {},
        .unnormalizedCoordinates = false,
    });
    return ret;
}

struct Pipeline {
    vk::UniquePipelineLayout pipelineLayout;
    vk::UniquePipeline pipeline;
};
Pipeline makeGraphicsPipeline(
    const GraphicsContext& vlk,
    std::span<const vk::DescriptorSetLayout> descriptorSetLayouts,
    const vk::UniqueRenderPass& renderPass,
    uint32_t subpass
) {
    Pipeline p;
    p.pipelineLayout = vlk.device->createPipelineLayoutUnique({
        .flags = {},
        .setLayoutCount = (uint32_t) descriptorSetLayouts.size(),
        .pSetLayouts = descriptorSetLayouts.data(),
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr,
    });
    const auto vertShader = vlk.createShaderModuleUnique("shaders/triangle.vert.spv");
    const auto fragShader = vlk.createShaderModuleUnique("shaders/triangle.frag.spv");
    struct Vertex {
        Vector3 pos;
        Vector2 uv;
        constexpr bool operator==(const Vertex&) const = default;
    };
    static constexpr vk::VertexInputBindingDescription bindingDescription = {
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = vk::VertexInputRate::eVertex,
    };
    static constexpr auto attributeDescriptions = std::to_array({
        vk::VertexInputAttributeDescription {
            .location = 0,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = offsetof(Vertex, pos),
        },
        vk::VertexInputAttributeDescription {
            .location = 1,
            .binding = 0,
            .format = vk::Format::eR32G32Sfloat,
            .offset = offsetof(Vertex, uv),
        },
    });
    p.pipeline = [&] {
        const auto shaderStages = std::to_array({
            vlk.genShaderStageCreateInfo(vk::ShaderStageFlagBits::eVertex, vertShader),
            vlk.genShaderStageCreateInfo(vk::ShaderStageFlagBits::eFragment, fragShader),
        });
        vk::PipelineVertexInputStateCreateInfo vertexInputState = {
            .flags = {},
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &bindingDescription,
            .vertexAttributeDescriptionCount = (uint32_t) attributeDescriptions.size(),
            .pVertexAttributeDescriptions = attributeDescriptions.data(),
        };
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState = {
            .flags = {},
            .topology = vk::PrimitiveTopology::eTriangleList,
            .primitiveRestartEnable = VK_FALSE,
        };
        vk::PipelineViewportStateCreateInfo viewportState = {
            .flags = {},
            .viewportCount = 1,     // required
            .pViewports = nullptr,  // ignored (dynamic)
            .scissorCount = 1,      // required
            .pScissors = nullptr,   // ignored (dynamic)
        };
        vk::PipelineRasterizationStateCreateInfo rasterizationState = {
            .flags = {},
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = vk::CullModeFlagBits::eBack,
            .frontFace = vk::FrontFace::eCounterClockwise,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0,
            .depthBiasClamp = 0,
            .depthBiasSlopeFactor = 0,
            .lineWidth = 1,
        };
        vk::PipelineMultisampleStateCreateInfo multisampleState = {
            .flags = {},
            .rasterizationSamples = vlk.props.maxSampleCount,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        };
        vk::PipelineDepthStencilStateCreateInfo depthStencilState = {
            .flags = {},
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = vk::CompareOp::eLess,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = {},
            .back = {},
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f,
        };
        vk::PipelineColorBlendAttachmentState colorBlendAttachment = {
            .blendEnable = VK_FALSE,
            .srcColorBlendFactor = vk::BlendFactor::eOne,
            .dstColorBlendFactor = vk::BlendFactor::eZero,
            .colorBlendOp = vk::BlendOp::eAdd,
            .srcAlphaBlendFactor = vk::BlendFactor::eOne,
            .dstAlphaBlendFactor = vk::BlendFactor::eZero,
            .alphaBlendOp = vk::BlendOp::eAdd,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        vk::PipelineColorBlendStateCreateInfo colorBlendState = {
            .flags = {},
            .logicOpEnable = VK_FALSE,
            .logicOp = vk::LogicOp::eCopy,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment,
            .blendConstants = std::to_array<float>({0, 0, 0, 0}),
        };
        constexpr auto dynamicStates = std::to_array({
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor,
        });
        vk::PipelineDynamicStateCreateInfo dynamicState = {
            .flags = {},
            .dynamicStateCount = (uint32_t) dynamicStates.size(),
            .pDynamicStates = dynamicStates.data(),
        };
        vk::GraphicsPipelineCreateInfo pipelineInfo = {
            .flags = {},
            .stageCount = (uint32_t) shaderStages.size(),
            .pStages = shaderStages.data(),
            .pVertexInputState = &vertexInputState,
            .pInputAssemblyState = &inputAssemblyState,
            .pTessellationState = nullptr,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizationState,
            .pMultisampleState = &multisampleState,
            .pDepthStencilState = &depthStencilState,
            .pColorBlendState = &colorBlendState,
            .pDynamicState = &dynamicState,
            .layout = p.pipelineLayout.get(),
            .renderPass = renderPass.get(),
            .subpass = subpass,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1,
        };
        return vlk.device->createGraphicsPipelineUnique(nullptr, pipelineInfo).value;
    }();
    return p;
}

// Descriptor pool tied to a DescriptorSetLayout
struct TypedDescriptorPool {
    const GraphicsContext* vlk;
    vk::UniqueDescriptorSetLayout descriptorSetLayout;
    vk::UniqueDescriptorPool descriptorPool;
    std::vector<vk::UniqueDescriptorSet> alloc(uint32_t n) const {
        return vlk->device->allocateDescriptorSetsUnique({
            .descriptorPool = descriptorPool.get(),
            .descriptorSetCount = n,
            .pSetLayouts = std::vector(n, descriptorSetLayout.get()).data(),
        });
    }
    vk::UniqueDescriptorSet alloc() const {
        return std::move(vlk->device->allocateDescriptorSetsUnique({
            .descriptorPool = descriptorPool.get(),
            .descriptorSetCount = 1,
            .pSetLayouts = &descriptorSetLayout.get(),
        })[0]);
    }
};
TypedDescriptorPool makeTypedDescriptorPool(
    const GraphicsContext& vlk,
    std::span<const vk::DescriptorSetLayoutBinding> materialBindings,
    size_t count
) {
    constexpr auto genPoolSizes = [](std::span<const vk::DescriptorSetLayoutBinding> bindings) {
        std::map<vk::DescriptorType, uint32_t> poolSizes;
        for (const auto& e : bindings) {
            poolSizes[e.descriptorType]++;
        }
        std::vector<vk::DescriptorPoolSize> ret;
        ret.reserve(poolSizes.size());
        for (const auto& [type, count] : poolSizes) {
            ret.push_back({
                .type = type,
                .descriptorCount = count,
            });
        }
        return ret;
    };
    return TypedDescriptorPool {
        .vlk = &vlk,
        .descriptorSetLayout = vlk.createDescriptorSetLayoutUnique(materialBindings),
        .descriptorPool = vlk.createDescriptorPoolUnique(genPoolSizes(materialBindings), count),
    };
}

struct MappedBuffer {
    std::pair<vk::UniqueBuffer, vk::UniqueDeviceMemory> buffer;
    void* mapping;
};
MappedBuffer makeMappedBuffer(const GraphicsContext& vlk, uint32_t size, vk::BufferUsageFlags usage) {
    MappedBuffer ret;
    ret.buffer = vlk.createBufferUnique(size, usage, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    ret.mapping = vlk.device->mapMemory(ret.buffer.second.get(), 0, size, {});
    return ret;
}

// TODO detach material type and material instance from model
struct Model {
    Mesh mesh;
    Texture texture;
    TypedDescriptorPool materialDescriptorPool;
    vk::UniqueDescriptorSet materialDescriptorSet;
};
Model makeModel(
    const GraphicsContext& vlk,
    std::string_view meshPath,
    std::string_view texturePath
) {
    Model ret;
    ret.mesh = makeMesh(vlk, meshPath);
    ret.texture = makeTexture(vlk, texturePath, vk::Format::eR8G8B8A8Srgb);
    ret.materialDescriptorPool = makeTypedDescriptorPool(vlk, std::to_array({
        vk::DescriptorSetLayoutBinding {
            .binding = 0,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
            .pImmutableSamplers = nullptr,
        },
    }), 1);
    ret.materialDescriptorSet = ret.materialDescriptorPool.alloc();
    vlk.device->updateDescriptorSets({
        vk::WriteDescriptorSet {
            .dstSet = ret.materialDescriptorSet.get(),
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = std::to_array({vk::DescriptorImageInfo {
                .sampler = ret.texture.sampler.get(),
                .imageView = ret.texture.imageView.get(),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            }}).data(),
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        },
    }, nullptr);
    return ret;
}

struct ModelInstance {
    struct InstanceUboData {
        alignas(16) Matrix4 MVP;
    };
    const Model* model;
    vk::UniqueDescriptorSet descriptorSet;
    MappedBuffer ubo;
    void updateUbo(const Transform& transform, const Transform& camera, vk::Extent2D imageExtent) const {
        Matrix4 model = transform.Matrix();
        Matrix4 view = Transform::z_convert * camera.Matrix().Inverse();
        float aspect = (float) imageExtent.width / imageExtent.height;
        Matrix4 proj = Transform::PerspectiveProjection(90, aspect, {0.1, 500}) * Transform::y_flip;
        // Matrix4 proj = Transform::OrthgraphicProjection(2, aspect, {0.1, 10}) * Transform::y_flip;
        InstanceUboData uboData = {
            .MVP = (proj * view * model).Transposed(),
        };
        memcpy(ubo.mapping, &uboData, sizeof(uboData));
    };
};
struct ModelInstancePool {
    const Model* model;
    TypedDescriptorPool descriptorPool;
    ModelInstance alloc() const {
        ModelInstance ret;
        ret.model = model;
        ret.descriptorSet = descriptorPool.alloc();
        ret.ubo = makeMappedBuffer(*descriptorPool.vlk, sizeof(ModelInstance::InstanceUboData), vk::BufferUsageFlagBits::eUniformBuffer);
        descriptorPool.vlk->device->updateDescriptorSets({
            vk::WriteDescriptorSet {
                .dstSet = ret.descriptorSet.get(),
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pImageInfo = nullptr,
                .pBufferInfo = std::to_array({vk::DescriptorBufferInfo {
                    .buffer = ret.ubo.buffer.first.get(),
                    .offset = 0,
                    .range = vk::WholeSize,
                }}).data(),
                .pTexelBufferView = nullptr,
            },
        }, nullptr);
        return ret;
    }
    Pipeline makePipeline(const vk::UniqueRenderPass& renderPass, uint32_t subpass) const {
        return makeGraphicsPipeline(*descriptorPool.vlk, std::to_array({
            model->materialDescriptorPool.descriptorSetLayout.get(),
            descriptorPool.descriptorSetLayout.get()
        }), renderPass, subpass);
    }
};
ModelInstancePool makeModelInstancePool(
    const GraphicsContext& vlk,
    const Model* model,
    size_t nObjects
) {
    ModelInstancePool ret;
    ret.model = model;
    ret.descriptorPool = makeTypedDescriptorPool(vlk, std::to_array({
        vk::DescriptorSetLayoutBinding {
            .binding = 0,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eVertex,
            .pImmutableSamplers = nullptr,
        },
    }), nObjects);
    return ret;
}

struct ForwardRenderer {
    vk::RenderPass renderPass;
    vk::Framebuffer framebuffer;
    vk::Extent2D imageExtent;
    vk::Pipeline graphicsPipeline;
    vk::PipelineLayout pipelineLayout;
    std::span<const vk::ClearValue> clearValues;

    // Cache for renderOne()
    const Model* pLastModel = nullptr;

    void begin(const vk::UniqueCommandBuffer& commandBuffer) {
        pLastModel = nullptr;
        commandBuffer->begin({
            .flags = {},
            .pInheritanceInfo = nullptr,
        });
        commandBuffer->beginRenderPass({
            .renderPass = this->renderPass,
            .framebuffer = this->framebuffer,
            .renderArea = {
                .offset = {0, 0},
                .extent = this->imageExtent,
            },
            .clearValueCount = (uint32_t) this->clearValues.size(),
            .pClearValues = this->clearValues.data(),
        }, vk::SubpassContents::eInline);
        commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, this->graphicsPipeline);
        commandBuffer->setViewport(0, vk::Viewport {
            .x = 0,
            .y = 0,
            .width = (float) this->imageExtent.width,
            .height = (float) this->imageExtent.height,
            .minDepth = 0,
            .maxDepth = 1,
        });
        commandBuffer->setScissor(0, vk::Rect2D {
            .offset = {0, 0},
            .extent = this->imageExtent,
        });
    }

    void render(
        const vk::UniqueCommandBuffer& commandBuffer,
        const Mesh& mesh,
        const std::ranges::contiguous_range auto& descriptorSets,
        const std::ranges::input_range auto& instanceDescriptorSets
    ) {
        commandBuffer->bindVertexBuffers(0, {mesh.vertexBuffer.first.get()}, {0});
        if (mesh.indexed) {
            commandBuffer->bindIndexBuffer(mesh.indexBuffer.first.get(), 0, vk::IndexType::eUint32);
        }
        commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, this->pipelineLayout, 0, descriptorSets, nullptr);
        for (const auto& instanceDescriptorSet : instanceDescriptorSets) {
            commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, this->pipelineLayout, descriptorSets.size(), instanceDescriptorSet, nullptr);
            if (mesh.indexed) {
                commandBuffer->drawIndexed(mesh.nIndices, 1, 0, 0, 0);
            } else {
                commandBuffer->draw(mesh.nVertices, 1, 0, 0);
            }
        }
    }

    void renderOne(
        const vk::UniqueCommandBuffer& commandBuffer,
        const Model& model,
        const vk::DescriptorSet& instanceDescriptorSet
    ) {
        const Mesh& mesh = model.mesh;
        if (pLastModel != &model) {
            commandBuffer->bindVertexBuffers(0, {mesh.vertexBuffer.first.get()}, {0});
            if (mesh.indexed) {
                commandBuffer->bindIndexBuffer(mesh.indexBuffer.first.get(), 0, vk::IndexType::eUint32);
            }
            commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, this->pipelineLayout, 0, model.materialDescriptorSet.get(), nullptr);
            pLastModel = &model;
        }
        commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, this->pipelineLayout, 1, instanceDescriptorSet, nullptr);
        if (mesh.indexed) {
            commandBuffer->drawIndexed(mesh.nIndices, 1, 0, 0, 0);
        } else {
            commandBuffer->draw(mesh.nVertices, 1, 0, 0);
        }
    }

    void end(const vk::UniqueCommandBuffer& commandBuffer) {
        commandBuffer->endRenderPass();
        commandBuffer->end();
    }
};

struct CombinedModel {
    std::unique_ptr<Model> pModel;
    ModelInstancePool instancePool;
    ModelInstance alloc() const { return instancePool.alloc(); }
};
CombinedModel makeCombinedModel(
    const GraphicsContext& vlk,
    std::string_view meshPath,
    std::string_view texturePath,
    uint32_t nObjects
) {
    CombinedModel ret;
    ret.pModel = std::make_unique<Model>(makeModel(vlk, meshPath, texturePath));
    ret.instancePool = makeModelInstancePool(vlk, ret.pModel.get(), nObjects);
    return ret;
}

struct StaticObject {
    ModelInstance modelInstance;
    Transform transform;
};

struct Player {
    Transform transform = {{0, 0, 0}};
    Vector3 euler = Vector3(0);
    Vector3 velocity = Vector3(0);
    void update(float dt, GLFWwindow* window) {
        constexpr float accel = 30;
        constexpr float airAccel = 300;
        constexpr float maxSpeed = 3.0;
        constexpr float airMaxSpeed = 0.3;
        transform.rotation = Quaternion::Euler(euler += input::get_rotation(window) * 3 * dt);
        const Vector3 rawMove = input::get_move(window);
        const Vector2 move = Vector2::Rotate(rawMove.xy().SafeNormalized(), euler.z) * dt;
        const bool touchingGround = transform.position.z == 0;
        const bool jumping = rawMove.z > 0;
        const bool inAir = !touchingGround || jumping;
        // Friction
        if (!inAir) {
            const Vector2 badVel = move ? Vector2::ProjectionOnPlane(velocity.xy(), move) : velocity.xy();
            if (badVel) {
                const float friction = 10 * dt;
                velocity -= Vector3(badVel.ClampedMagnitude(friction), 0);
            }
        }
        // Move
        {
            const float curSpeed = Vector2::ProjectionLength(velocity.xy(), move);
            const float missingSpeed = std::max(0.0f, (inAir ? airMaxSpeed : maxSpeed) - curSpeed);
            if (move) { velocity += Vector3((move * (inAir ? airAccel : accel)).ClampedMagnitude(missingSpeed), 0); }
        }
        // Gravity
        velocity += Vector3(0, 0, -10 * dt);
        // Jump
        if (touchingGround && jumping) { velocity.z = 3.0; }
        // Apply velocity
        transform.position += velocity * dt;
        // Floor collision
        if (transform.position.z < 0) {
            transform.position.z = 0;
            velocity.z = 0;
        }
    }
};

void applySystem(const auto& fn, auto&... objectRanges) {
    (std::ranges::for_each(objectRanges, fn), ...);
}

int main() {
    auto vlk = makeGraphicsContext();

    constexpr size_t nCubes = 500;
    const auto cubeModel = makeCombinedModel(vlk, "models/cube.obj", "textures/bricks.png", nCubes);
    const auto graphicsPipeline = cubeModel.instancePool.makePipeline(vlk.renderPass, 0);

    const auto planeModel = makeCombinedModel(vlk, "models/plane.obj", "textures/white.png", 1);

    std::vector<StaticObject> sceneObjects;
    for (size_t i = 0; i < nCubes; i++) {
        sceneObjects.push_back({
            .modelInstance = cubeModel.alloc(),
            .transform = {
                .position = {i * 2.0f, 0, 0},
                .rotation = Quaternion::Identity(),
                .scale = Vector3(1),
            }
        });
    }
    sceneObjects.push_back({
        .modelInstance = planeModel.alloc(),
        .transform = {
            .position = {0, 1, -0.5},
            .rotation = Quaternion::Identity(),
            .scale = Vector3(10),
        }
    });

    std::vector<Player> players;
    players.push_back({
        .transform = {{0, 2, 0}},
        .euler = {0, 0, std::numbers::pi},
    });

    const auto clearColors = std::to_array<vk::ClearValue>({
        {
            .color = {
                .float32 = std::to_array<float>({0, 0, 0, 1})
            },
        }, {
            .depthStencil = {
                .depth = 1.0f,
            },
        },
    });

    // const auto processInput = [&](float dt) {
    //     camera.rotation = camera.rotation * Quaternion::Euler(input::get_rotation(vlk.window.get()) * 3 * dt);
    //     camera.position += camera.rotation.Rotate(input::get_move(vlk.window.get()) * 0.75 * dt);
    // };

    const auto drawFrame = [&](float time, float dt, size_t frameIndex) {
        (void) time, (void) dt;
        bool framebufferResized = false;
        const auto& currentFrame = vlk.framesInFlight[frameIndex % vlk.maxFramesInFlight];
        (void) vlk.device->waitForFences(currentFrame.inFlightFence.get(), VK_TRUE, -1);
        vk::Result res;
        uint32_t imageIndex;
        try {
            std::tie(res, imageIndex) = vlk.device->acquireNextImageKHR(vlk.swapchain.swapchain.get(), -1, currentFrame.imageAvailableSemaphore.get(), nullptr);
        } catch (const vk::OutOfDateKHRError& e) {
            framebufferResized = true;
            return framebufferResized;
        }
        if (res == vk::Result::eSuboptimalKHR) {
            // This is not an error, which means that
            // imageAvailableSemaphore was signaled,
            // so we can't return without waiting on it
            framebufferResized = true;
        }
        vlk.device->resetFences(currentFrame.inFlightFence.get());
        ForwardRenderer renderer = {
            .renderPass = vlk.renderPass.get(),
            .framebuffer = vlk.swapchain.framebuffers[imageIndex].get(),
            .imageExtent = vlk.swapchain.info.imageExtent,
            .graphicsPipeline = graphicsPipeline.pipeline.get(),
            .pipelineLayout = graphicsPipeline.pipelineLayout.get(),
            .clearValues = clearColors,
        };
        renderer.begin(currentFrame.commandBuffer);
        applySystem([&](const auto& e) {
            renderer.renderOne(
                currentFrame.commandBuffer,
                *e.modelInstance.model,
                e.modelInstance.descriptorSet.get()
            );
        }, sceneObjects);
        renderer.end(currentFrame.commandBuffer);
        constexpr vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
        vlk.graphicsQueue.submit(vk::SubmitInfo {
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &currentFrame.imageAvailableSemaphore.get(),
            .pWaitDstStageMask = waitStages,
            .commandBufferCount = 1,
            .pCommandBuffers = &currentFrame.commandBuffer.get(),
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &currentFrame.renderFinishedSemaphore.get(),
        }, currentFrame.inFlightFence.get());
        try {
            res = vlk.presentQueue.presentKHR(vk::PresentInfoKHR {
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &currentFrame.renderFinishedSemaphore.get(),
                .swapchainCount = 1,
                .pSwapchains = &vlk.swapchain.swapchain.get(),
                .pImageIndices = &imageIndex,
                .pResults = nullptr,
            });
        } catch (const vk::OutOfDateKHRError& e) {
            framebufferResized = true;
        }
        if (res == vk::Result::eSuboptimalKHR) {
            framebufferResized = true;
        }
        return framebufferResized;
    };

    // Main loop
    [&] {
        FrameCounter frameCounter;
        const Stopwatch globalTime;
        while (!glfwWindowShouldClose(vlk.window.get())) {
            glfwPollEvents();
            const float dt = frameCounter.deltaTime / 1000;
            const float time = globalTime.ping() / 1000;
            // processInput(dt);
            applySystem([&](auto& player) {
                player.update(dt, vlk.window.get());
            }, players);
            applySystem([&](const auto& e) {
                e.modelInstance.updateUbo(e.transform, players[0].transform, vlk.swapchain.info.imageExtent);
            }, sceneObjects);
            const bool framebufferResized = drawFrame(time, dt, frameCounter.frameCount);
            frameCounter.tick();
            if (frameCounter.frameCount == 0) {
                const double t = globalTime.ping() / 1000;
                const double fc = frameCounter.frameCount;
                prn_raw(t, " s total, ", t/fc*1000, " ms avg (", fc/t, " fps)");
                break;
            }
            if (framebufferResized) {
                prn("Framebuffer resized");
                int w, h;
                glfwGetFramebufferSize(vlk.window.get(), &w, &h);
                if (w == 0 || h == 0) {
                    prn("Window is minimized, waiting...");
                    while (w == 0 || h == 0) {
                        glfwWaitEvents();
                        glfwGetFramebufferSize(vlk.window.get(), &w, &h);
                    }
                }
                vlk.device->waitIdle();
                vlk.recreateSwapchainUnique();
                vlk.recreateFramebuffersUnique();
            }
        }
    }();
    vlk.device->waitIdle();
}
