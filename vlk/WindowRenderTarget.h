#pragma once
#include <functional>
#include <memory>
#include "GraphicsContext.h"
#include "utils.h"

struct WindowSurface {
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window = {nullptr, glfwDestroyWindow};
    vk::UniqueSurfaceKHR surface;
};
inline auto createWindowSurface(vk::Instance instance) {
    auto window = [w = 800, h = 600] {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        // glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        auto ptr = glfwCreateWindow(w, h, "Vulkan", nullptr, nullptr);
        return std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)>(ptr, glfwDestroyWindow);
    }();
    auto surface = [&] {
        VkSurfaceKHR ret;
        exwrap(glfwCreateWindowSurface(instance, window.get(), nullptr, &ret));
        return vk::UniqueSurfaceKHR(ret, instance);
    }();
    return WindowSurface {
        .window = std::move(window),
        .surface = std::move(surface),
    };
};

class WindowRenderTarget {
private:
    const GraphicsContext* vlk;
    const WindowSurface* windowSurface;

    struct SwapchainResources {
        vk::SwapchainCreateInfoKHR         info;
        vk::UniqueSwapchainKHR             swapchain;
        std::vector<vk::UniqueImageView>   imageViewResources;
        std::vector<vk::ImageView>         imageViews;
    } swapchain;

private:
    vk::UniqueCommandPool frameCommandPool;
    struct FrameInFlight {
        vk::UniqueCommandBuffer commandBuffer;
        vk::UniqueSemaphore imageAvailableSemaphore;
        vk::UniqueSemaphore renderFinishedSemaphore;
        vk::UniqueFence inFlightFence;
    };
    static constexpr uint32_t maxFramesInFlight = 1;
    std::array<FrameInFlight, maxFramesInFlight> framesInFlight;

private:
    void createSwapchain() {
        swapchain.info = [&] {
            const auto caps = vlk->physicalDevice.getSurfaceCapabilitiesKHR(windowSurface->surface.get());
            const auto formats = vlk->physicalDevice.getSurfaceFormatsKHR(windowSurface->surface.get());
            const auto presentModes = vlk->physicalDevice.getSurfacePresentModesKHR(windowSurface->surface.get());
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
                glfwGetFramebufferSize(windowSurface->window.get(), &w, &h);
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
            for (const auto& e : vlk->props.uniqueQueueFamilies) { uniqueQueueFamiliesVec.push_back(e); }

            return vk::SwapchainCreateInfoKHR {
                .flags = {},
                .surface = windowSurface->surface.get(),
                .minImageCount = bestImageCount,
                .imageFormat = bestFormat.format,
                .imageColorSpace = bestFormat.colorSpace,
                .imageExtent = bestSwapExtent,
                .imageArrayLayers = 1,
                .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
                .imageSharingMode = vlk->props.uniqueQueueFamilies.size() > 1 ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive,
                .queueFamilyIndexCount = (uint32_t) uniqueQueueFamiliesVec.size(),
                .pQueueFamilyIndices = uniqueQueueFamiliesVec.data(),
                .preTransform = caps.currentTransform,
                .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
                .presentMode = bestPresentMode,
                .clipped = VK_TRUE,
                .oldSwapchain = swapchain.swapchain.get(),
            };
        }();
        swapchain.swapchain = vlk->device->createSwapchainKHRUnique(swapchain.info);
        swapchain.imageViewResources = [&] {
            std::vector<vk::UniqueImageView> ret;
            for (const auto& image : vlk->device->getSwapchainImagesKHR(swapchain.swapchain.get())) {
                ret.push_back(vlk->device->createImageViewUnique({
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
        swapchain.imageViews = [&] {
            std::vector<vk::ImageView> ret;
            for (const auto& e : swapchain.imageViewResources) {
                ret.push_back(e.get());
            }
            return ret;
        }();
    }

    void createFramesInFlight() {
        frameCommandPool = vlk->device->createCommandPoolUnique({
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = vlk->props.graphicsQueueFamily,
        });

        for (auto& f : framesInFlight) {
            f = {
                .commandBuffer = std::move(vlk->device->allocateCommandBuffersUnique({
                    .commandPool = frameCommandPool.get(),
                    .level = vk::CommandBufferLevel::ePrimary,
                    .commandBufferCount = 1,
                })[0]),
                .imageAvailableSemaphore = vlk->device->createSemaphoreUnique({}),
                .renderFinishedSemaphore = vlk->device->createSemaphoreUnique({}),
                .inFlightFence = vlk->device->createFenceUnique({
                    .flags = vk::FenceCreateFlagBits::eSignaled,
                }),
            };
        }
    }

    void recreateSwapchain() {
        vlk->device->waitIdle();
        createSwapchain();
        if (onRecreateSwapchain) {
            onRecreateSwapchain();
        }
    }

    std::optional<Frame> activeFrame = std::nullopt;

public:

public:
    explicit WindowRenderTarget(const GraphicsContext* vlk, const WindowSurface* window)
        : vlk(vlk), windowSurface(window)
    {
        createSwapchain();
        createFramesInFlight();
    }

    std::function<void()> onRecreateSwapchain;

    RenderTarget renderTarget() const {
        return {
            .extent = swapchain.info.imageExtent,
            .format = swapchain.info.imageFormat,
            .imageViews = swapchain.imageViews,
        };
    }

    [[nodiscard]]
    std::optional<Frame> startFrame() {
        try {
            static_assert(maxFramesInFlight == 1);
            const uint32_t frameIndex = 0;
            const auto& frameResources = framesInFlight[frameIndex];
            (void) vlk->device->waitForFences(frameResources.inFlightFence.get(), VK_TRUE, -1);
            const uint32_t imageIndex = vlk->device->acquireNextImageKHR(swapchain.swapchain.get(), -1, frameResources.imageAvailableSemaphore.get(), nullptr).value;
            // vkAcquireNextImageKHR may return vk::Result::eSuboptimalKHR.
            // This is not an error, which means that
            // imageAvailableSemaphore was signaled,
            // so we can't return early without waiting on it
            vlk->device->resetFences(frameResources.inFlightFence.get());
            return activeFrame = Frame {
                .commandBuffer = frameResources.commandBuffer.get(),
                .frameIndex = frameIndex,
                .imageIndex = imageIndex,
            };
        } catch (const vk::OutOfDateKHRError& e) {
            // Can be thrown from vkAcquireNextImageKHR or vkQueuePresentKHR
            recreateSwapchain();
        }
        return std::nullopt;
    }

    void endFrame() {
        assert(activeFrame.has_value());
        try {
            const auto& frameResources = framesInFlight[activeFrame->frameIndex];
            vlk->graphicsQueue.submit(vk::SubmitInfo {
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &frameResources.imageAvailableSemaphore.get(),
                .pWaitDstStageMask = std::to_array<vk::PipelineStageFlags>({ vk::PipelineStageFlagBits::eColorAttachmentOutput }).data(),
                .commandBufferCount = 1,
                .pCommandBuffers = &frameResources.commandBuffer.get(),
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &frameResources.renderFinishedSemaphore.get(),
            }, frameResources.inFlightFence.get());
            const vk::Result presentRes = vlk->presentQueue.presentKHR(vk::PresentInfoKHR {
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &frameResources.renderFinishedSemaphore.get(),
                .swapchainCount = 1,
                .pSwapchains = &swapchain.swapchain.get(),
                .pImageIndices = &activeFrame->imageIndex,
                .pResults = nullptr,
            });
            if (presentRes == vk::Result::eSuboptimalKHR) {
                recreateSwapchain();
            }
        } catch (const vk::OutOfDateKHRError& e) {
            // Can be thrown from vkAcquireNextImageKHR or vkQueuePresentKHR
            recreateSwapchain();
        }
        activeFrame = std::nullopt;
    }
};
