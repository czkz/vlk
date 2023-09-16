#include "Camera.h"
#include <cstddef>
#include <memory>
#include <set>
#include <fstream>
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

int main() {
    const struct glfwInstanceGuard {
        glfwInstanceGuard() { exwrapb(glfwInit()); }
        ~glfwInstanceGuard() { glfwTerminate(); }
        glfwInstanceGuard(glfwInstanceGuard&&) = delete;
    } glfwInstance;

    // Create window
    const auto window = [w = 800, h = 600] {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        // glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        auto ptr = glfwCreateWindow(w, h, "Vulkan", nullptr, nullptr);
        return std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)>(ptr, glfwDestroyWindow);
    }();

    // Create Vulkan instance
    const auto instance = [] {
        // List of required instance layers
        const auto requiredInstanceLayers = std::to_array({
            "VK_LAYER_KHRONOS_validation",
        });
        // const std::array<const char*, 0> requiredInstanceLayers;

        // Check layer support
        [&requiredInstanceLayers] {
            const auto availableInstanceLayers = vk::enumerateInstanceLayerProperties();

            prn("Available layers:");
            for (const auto& e : availableInstanceLayers) { prn('\t', std::string_view(e.layerName)); }

            for (const auto& name : requiredInstanceLayers) {
                if (!std::ranges::any_of(availableInstanceLayers, [name](const auto& e) { return std::string_view(e.layerName) == name; })) {
                    throw ex::runtime(fmt("Layer", name, "not available"));
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
                    throw ex::runtime(fmt("Extension", name, "not available"));
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

            const vk::InstanceCreateInfo createInfo = {
                .flags = {},
                .pApplicationInfo = &appInfo,
                .enabledLayerCount = (uint32_t) requiredInstanceLayers.size(),
                .ppEnabledLayerNames = requiredInstanceLayers.data(),
                .enabledExtensionCount = (uint32_t) requiredInstanceExtensions.size(),
                .ppEnabledExtensionNames = requiredInstanceExtensions.data(),
            };

            return vk::createInstanceUnique(createInfo);
        }();
    }();

    // Create window surface
    const auto surface = [&instance, &window] {
        VkSurfaceKHR ret;
        exwrap(glfwCreateWindowSurface(instance.get(), window.get(), nullptr, &ret));
        return vk::UniqueSurfaceKHR(ret, instance.get());
    }();

    // List of required device extensions
    const auto requiredDeviceExtensions = std::to_array({
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    });

    // Pick physical device
    const auto physicalDevice = [&instance, &surface = surface.get(), &requiredDeviceExtensions] {
        // Get a list of available devices
        const auto availableDevices = instance->enumeratePhysicalDevices();
        if (availableDevices.size() == 0) {
            throw ex::runtime("Couldn't find devices with Vulkan support");
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
        const auto isSuitable = [&requiredDeviceExtensions, &surface](const vk::PhysicalDevice& device) {
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
            const bool hasGraphicsQueueFamily = [&queueFamilies] {
                for (uint32_t i = 0; i < queueFamilies.size(); i++) {
                    if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                        return true;
                    }
                }
                return false;
            }();
            const bool hasPresentQueueFamily = [&device, &surface, len = queueFamilies.size()] {
                for (uint32_t i = 0; i < len; i++) {
                    if (device.getSurfaceSupportKHR(i, surface)) {
                        return true;
                    }
                }
                return false;
            }();
            return
                deviceSupportsExtensions &&
                hasGraphicsQueueFamily &&
                hasPresentQueueFamily &&
                device.getSurfaceFormatsKHR(surface).size() > 0 &&
                device.getSurfacePresentModesKHR(surface).size() > 0 &&
                device.getFeatures().samplerAnisotropy;
        };

        const auto suitableDevices = [&availableDevices, &isSuitable] {
            std::vector<vk::PhysicalDevice> ret;
            std::ranges::copy_if(availableDevices, std::back_inserter(ret), isSuitable);
            return ret;
        }();
        if (suitableDevices.size() == 0) {
            throw ex::runtime("Couldn't find a suitable device");
        }

        prn("Suitable physical devices:");
        for (const auto& device : suitableDevices) {
            prn("\t", std::string_view(device.getProperties().deviceName));
        }

        // Pick best
        return [&suitableDevices] {
            const auto compare = [](const vk::PhysicalDevice& a, const vk::PhysicalDevice& b) {
                const auto typeToPriority = [](const auto& type) {
                    using t = vk::PhysicalDeviceType;
                    if (t::eDiscreteGpu == type) {
                        return 0;
                    } else if (t::eOther == type) {
                        return 1;
                    } else if (t::eIntegratedGpu == type) {
                        return 2;
                    } else if (t::eVirtualGpu == type) {
                        return 3;
                    }
                    assert(false);
                };
                return
                    typeToPriority(a.getProperties().deviceType) <
                    typeToPriority(a.getProperties().deviceType) ||
                    a.getMemoryProperties().memoryHeaps.at(0).size >
                    b.getMemoryProperties().memoryHeaps.at(0).size;
            };
            return *std::ranges::max_element(suitableDevices, compare);
        }();
    }();
    prn("Chosen physical device:", std::string_view(physicalDevice.getProperties().deviceName));

    // Get queue family indices for chosen device
    const auto graphicsQueueFamily = [&physicalDevice] {
        const auto queueFamilies = physicalDevice.getQueueFamilyProperties();
        for (uint32_t i = 0; i < queueFamilies.size(); i++) {
            if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                return i;
            }
        }
        assert(false);
    }();
    const auto presentQueueFamily = [&physicalDevice, &surface] {
        const auto len = physicalDevice.getQueueFamilyProperties().size();
        for (uint32_t i = 0; i < len; i++) {
            if (physicalDevice.getSurfaceSupportKHR(i, surface.get())) {
                return i;
            }
        }
        assert(false);
    }();
    const std::set<uint32_t> uniqueQueueFamilies = {graphicsQueueFamily, presentQueueFamily};

    // Create logical device
    const auto device = [
        physicalDevice,
        &extensions = requiredDeviceExtensions,
        &uniqueQueueFamilies
    ] {
        std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
        for (const auto& e : uniqueQueueFamilies) {
            float queuePriority = 1.0f;
            queueCreateInfos.push_back({
                .flags = {},
                .queueFamilyIndex = e,
                .queueCount = 1,
                .pQueuePriorities = &queuePriority,
            });
        }
        const vk::PhysicalDeviceFeatures usedFeatures {
            .samplerAnisotropy = VK_TRUE,
        };
        const vk::DeviceCreateInfo deviceCreateInfo = {
            .flags = {},
            .queueCreateInfoCount = (uint32_t) queueCreateInfos.size(),
            .pQueueCreateInfos = queueCreateInfos.data(),
            .enabledExtensionCount = (uint32_t) extensions.size(), // Device extensions, not instance extensions
            .ppEnabledExtensionNames = extensions.data(),
            .pEnabledFeatures = &usedFeatures,
        };
        return physicalDevice.createDeviceUnique(deviceCreateInfo);
    }();

    const vk::Queue graphicsQueue = device->getQueue(graphicsQueueFamily, 0);
    const vk::Queue presentQueue = device->getQueue(presentQueueFamily, 0);

    struct Vertex {
        Vector3 pos;
        Vector2 uv;
    };
    constexpr vk::VertexInputBindingDescription bindingDescription = {
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = vk::VertexInputRate::eVertex,
    };
    constexpr auto attributeDescriptions = std::to_array<vk::VertexInputAttributeDescription>({
        {
            .location = 0,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = offsetof(Vertex, pos),
        }, {
            .location = 1,
            .binding = 0,
            .format = vk::Format::eR32G32Sfloat,
            .offset = offsetof(Vertex, uv),
        },
    });


    const auto pickSwapchainSettings = [
        &physicalDevice,
        &surface = surface.get(),
        &window,
        &uniqueQueueFamilies
    ](vk::SwapchainKHR oldSwapchain) {
        const auto caps = physicalDevice.getSurfaceCapabilitiesKHR(surface);
        const auto formats = physicalDevice.getSurfaceFormatsKHR(surface);
        const auto presentModes = physicalDevice.getSurfacePresentModesKHR(surface);
        vk::SurfaceFormatKHR bestFormat = [&formats = formats] {
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
            const auto preferredModes = std::to_array<vk::PresentModeKHR>({
                vk::PresentModeKHR::eFifo,        // Always force Vsync
            });
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
        vk::Extent2D bestSwapExtent = [&caps, &window] {
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
        for (const auto& e : uniqueQueueFamilies) { uniqueQueueFamiliesVec.push_back(e); }

        vk::SwapchainCreateInfoKHR createInfo = {
            .flags = {},
            .surface = surface,
            .minImageCount = bestImageCount,
            .imageFormat = bestFormat.format,
            .imageColorSpace = bestFormat.colorSpace,
            .imageExtent = bestSwapExtent,
            .imageArrayLayers = 1,
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
            .imageSharingMode = uniqueQueueFamilies.size() > 1 ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive,
            .queueFamilyIndexCount = (uint32_t) uniqueQueueFamiliesVec.size(),
            .pQueueFamilyIndices = uniqueQueueFamiliesVec.data(),
            .preTransform = caps.currentTransform,
            .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
            .presentMode = bestPresentMode,
            .clipped = VK_TRUE,
            .oldSwapchain = oldSwapchain,
        };
        return createInfo;
    };
    const auto createImageViewsUnique = [&device](const vk::SwapchainKHR& swapchain, vk::Format format) {
        std::vector<vk::UniqueImageView> ret;
        for (const auto& image : device->getSwapchainImagesKHR(swapchain)) {
            vk::ImageViewCreateInfo createInfo = {
                .flags = {},
                .image = image,
                .viewType = vk::ImageViewType::e2D,
                .format = format,
                .components = {
                    .r = vk::ComponentSwizzle::eIdentity,
                    .g = vk::ComponentSwizzle::eIdentity,
                    .b = vk::ComponentSwizzle::eIdentity,
                    .a = vk::ComponentSwizzle::eIdentity,
                },
                .subresourceRange = {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            };
            ret.push_back(device->createImageViewUnique(createInfo));
        }
        return ret;
    };
    const auto createRenderPassUnique = [&device](vk::Format format) {
        vk::AttachmentDescription colorAttachmentDesc = {
            .flags = {},
            .format = format,
            .samples = vk::SampleCountFlagBits::e1,
            .loadOp = vk::AttachmentLoadOp::eClear,
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
        vk::SubpassDescription subpass = {
            .flags = {},
            .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
            .inputAttachmentCount = 0,
            .pInputAttachments = nullptr,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentRef,
            .pResolveAttachments = nullptr,
            .pDepthStencilAttachment = nullptr,
            .preserveAttachmentCount = 0,
            .pPreserveAttachments = nullptr,
        };
        vk::SubpassDependency extenralDependency = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
            .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits::eNone,
            .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
            .dependencyFlags = {},
        };
        vk::RenderPassCreateInfo createInfo = {
            .flags = {},
            .attachmentCount = 1,
            .pAttachments = &colorAttachmentDesc,
            .subpassCount = 1,
            .pSubpasses = &subpass,
            .dependencyCount = 1,
            .pDependencies = &extenralDependency,
        };
        return device->createRenderPassUnique(createInfo);
    };
    const auto createFramebuffersUnique = [&device](std::span<const vk::UniqueImageView> imageViews, const vk::Extent2D& extent, const vk::RenderPass& renderPass) {
        std::vector<vk::UniqueFramebuffer> ret;
        for (const auto& imageView : imageViews) {
            vk::FramebufferCreateInfo createInfo = {
                .flags = {},
                .renderPass = renderPass,
                .attachmentCount = 1,
                .pAttachments = &imageView.get(),
                .width = extent.width,
                .height = extent.height,
                .layers = 1,
            };
            ret.push_back(device->createFramebufferUnique(createInfo));
        }
        return ret;
    };
    const auto createGraphicsPipelineUnique = [&device, &bindingDescription, &attributeDescriptions](
        vk::PipelineLayout pipelineLayout,
        vk::RenderPass renderPass,
        vk::ShaderModule vertShaderModule,
        vk::ShaderModule fragShaderModule
    ) {
        const auto genShaderStageCreateInfo = [](auto stage, auto module) {
            vk::PipelineShaderStageCreateInfo createInfo = {
                .flags = {},
                .stage = stage,
                .module = module,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            };
            return createInfo;
        };
        const auto shaderStages = std::to_array({
            genShaderStageCreateInfo(vk::ShaderStageFlagBits::eVertex, vertShaderModule),
            genShaderStageCreateInfo(vk::ShaderStageFlagBits::eFragment, fragShaderModule),
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
            .frontFace = vk::FrontFace::eClockwise,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0,
            .depthBiasClamp = 0,
            .depthBiasSlopeFactor = 0,
            .lineWidth = 1,
        };
        vk::PipelineMultisampleStateCreateInfo multisampleState = {
            .flags = {},
            .rasterizationSamples = vk::SampleCountFlagBits::e1,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
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
            .pDepthStencilState = nullptr,
            .pColorBlendState = &colorBlendState,
            .pDynamicState = &dynamicState,
            .layout = pipelineLayout,
            .renderPass = renderPass,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1,
        };
        return device->createGraphicsPipelineUnique(nullptr, pipelineInfo).value;
    };

    struct UniformBuffer {
        alignas(16) Matrix4 MVP;
    };
    const auto bindings = std::to_array<vk::DescriptorSetLayoutBinding>({
        {
            .binding = 0,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eVertex,
            .pImmutableSamplers = nullptr,
        },
        {
            .binding = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
            .pImmutableSamplers = nullptr,
        },
    });

    const auto descriptorSetLayout = device->createDescriptorSetLayoutUnique({
        .flags = {},
        .bindingCount = bindings.size(),
        .pBindings = bindings.data(),
    });

    // Create pipeline layout
    const auto pipelineLayout = [&device, &descriptorSetLayout] {
        vk::PipelineLayoutCreateInfo createInfo = {
            .flags = {},
            .setLayoutCount = 1,
            .pSetLayouts = &descriptorSetLayout.get(),
            .pushConstantRangeCount = 0,
            .pPushConstantRanges = nullptr,
        };
        return device->createPipelineLayoutUnique(createInfo);
    }();

    const auto createShaderModuleUnique = [&device](std::string_view code) {
        vk::ShaderModuleCreateInfo createInfo = {
            .flags = {},
            .codeSize = code.size(),
            .pCode = reinterpret_cast<const uint32_t*>(code.data()),
        };
        return device->createShaderModuleUnique(createInfo);
    };
    constexpr auto slurp = [](const char* filename) {
        std::ifstream f(filename);
        return std::string(std::istreambuf_iterator<char>(f), {});
    };
    const auto vertShader = createShaderModuleUnique(slurp("shaders/triangle.vert.spv"));
    const auto fragShader = createShaderModuleUnique(slurp("shaders/triangle.frag.spv"));

    vk::SwapchainCreateInfoKHR         swapchainInfo;
    vk::UniqueSwapchainKHR             swapchain;
    std::vector<vk::UniqueImageView>   swapchainImageViews;
    vk::UniqueRenderPass               renderPass;
    std::vector<vk::UniqueFramebuffer> swapchainFramebuffers;
    vk::UniquePipeline                 graphicsPipeline;

    const auto recreateSwapchainUnique = [&]() {
        swapchainInfo         = pickSwapchainSettings(swapchain.get());
        swapchain             = device->createSwapchainKHRUnique(swapchainInfo);
        swapchainImageViews   = createImageViewsUnique(swapchain.get(), swapchainInfo.imageFormat);
        renderPass            = createRenderPassUnique(swapchainInfo.imageFormat);
        swapchainFramebuffers = createFramebuffersUnique(swapchainImageViews, swapchainInfo.imageExtent, renderPass.get());
        graphicsPipeline      = createGraphicsPipelineUnique(pipelineLayout.get(), renderPass.get(), vertShader.get(), fragShader.get());
    };
    recreateSwapchainUnique();

    const auto findMemoryType = [&physicalDevice](uint32_t typeFilter, vk::MemoryPropertyFlags requiredProperties) {
        const auto memoryProperties = physicalDevice.getMemoryProperties();
        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
            if (
                (typeFilter & (1 << i)) &&
                (memoryProperties.memoryTypes[i].propertyFlags & requiredProperties) == requiredProperties
            ) {
                return i;
            }
        }
        throw ex::runtime("Couldn't find suitable memory type");
    };

    const auto createBufferUnique = [&device, &findMemoryType](size_t nBytes, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties) {
        // Create buffer
        vk::UniqueBuffer buffer = [&device, nBytes, usage] {
            vk::BufferCreateInfo createInfo = {
                .flags = {},
                .size = nBytes,
                .usage = usage,
                .sharingMode = vk::SharingMode::eExclusive,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr,
            };
            return device->createBufferUnique(createInfo);
        }();

        // Allocate memory for the buffer
        vk::UniqueDeviceMemory deviceMemory = [&device, &buffer, &properties, &findMemoryType] {
            auto memoryRequirements = device->getBufferMemoryRequirements(buffer.get());
            vk::MemoryAllocateInfo allocInfo = {
                .allocationSize = memoryRequirements.size,
                .memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, properties),
            };
            vk::UniqueDeviceMemory ret = device->allocateMemoryUnique(allocInfo);
            device->bindBufferMemory(buffer.get(), ret.get(), 0);
            return ret;
        }();
        return std::pair {
            std::move(buffer),
            std::move(deviceMemory)
        };
    };

    const auto fillBuffer = [&device](const vk::UniqueDeviceMemory& memory, const auto& data) {
        std::span bytes = data;
        void* mapped = device->mapMemory(memory.get(), 0, bytes.size_bytes(), {});
        memcpy(mapped, bytes.data(), bytes.size_bytes());
        device->unmapMemory(memory.get());
    };

    const auto createStagingBufferUnique = [&createBufferUnique, &fillBuffer](const auto& data) {
        auto ret = createBufferUnique(
            std::span(data).size_bytes(),
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        fillBuffer(ret.second, data);
        return ret;
    };

    const auto tempCommandBuffer = [
        &device,
        &queue = graphicsQueue,
        commandPoolUtil = device->createCommandPoolUnique({
            .flags = vk::CommandPoolCreateFlagBits::eTransient,
            .queueFamilyIndex = graphicsQueueFamily,
        })
    ]() {
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
        return TempCommandBuffer::make(std::move(commandBuffer), queue);
    };

    const auto cmdCopyBuffer = [](
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
    };

    const auto cmdCopyBufferToImage = [](
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
    };

    const auto createDeviceLocalBufferUnique = [
        &createBufferUnique,
        &createStagingBufferUnique,
        &cmdCopyBuffer,
        &tempCommandBuffer
    ](vk::BufferUsageFlags usage, const auto& data) {
        const auto bytes = std::as_bytes(std::span(data));
        auto stagingBuffer = createStagingBufferUnique(bytes);
        auto localBuffer = createBufferUnique(
            bytes.size(),
            vk::BufferUsageFlagBits::eTransferDst | usage,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        cmdCopyBuffer(tempCommandBuffer(), stagingBuffer.first, localBuffer.first, bytes.size());
        return localBuffer;
    };

    const auto createDeviceLocalImageUnique = [
        &device,
        &createStagingBufferUnique,
        &findMemoryType,
        &tempCommandBuffer,
        &cmdCopyBufferToImage
    ](
        const auto& imageData,
        size_t w,
        size_t h,
        const vk::Format& format
    ) {
        const auto stagingBuffer = createStagingBufferUnique(imageData);
        auto localImage = [&device, &findMemoryType, &w, &h, &format] {
            auto image = device->createImageUnique({
                .flags = {},
                .imageType = vk::ImageType::e2D,
                .format = format,
                .extent = vk::Extent3D {
                    .width = (uint32_t) w,
                    .height = (uint32_t) h,
                    .depth = 1,
                },
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = vk::SampleCountFlagBits::e1,
                .tiling = vk::ImageTiling::eOptimal,
                .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                .sharingMode = vk::SharingMode::eExclusive,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr, // For shared sharingMode
                .initialLayout = vk::ImageLayout::eUndefined,
            });
            auto memoryRequirements = device->getImageMemoryRequirements(image.get());
            vk::MemoryAllocateInfo allocInfo = {
                .allocationSize = memoryRequirements.size,
                .memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal),
            };
            vk::UniqueDeviceMemory memory = device->allocateMemoryUnique(allocInfo);
            device->bindImageMemory(image.get(), memory.get(), 0);
            return std::pair(std::move(image), std::move(memory));
        }();
        auto commandBuffer = tempCommandBuffer();
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
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        });
        cmdCopyBufferToImage(commandBuffer, stagingBuffer.first, localImage.first, w, h);
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
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        });
        commandBuffer.end();
        return localImage;
    };

    const auto createDeviceLocalTextureUnique = [&createDeviceLocalImageUnique](std::string_view path, vk::Format format) {
        const int channels = [&format] {
            switch (format) {
                case vk::Format::eR8Srgb:       return 1;
                case vk::Format::eR8G8Srgb:     return 2;
                case vk::Format::eR8G8B8Srgb:   return 3;
                case vk::Format::eR8G8B8A8Srgb: return 4;
                default: throw ex::runtime("unsupported image format");
            }
        }();
        const auto img = load_image(path, channels);
        return createDeviceLocalImageUnique(img, img.w, img.h, format);
    };

    const auto cubeMesh = primitives::generate_cube(1);
    const auto vertices = [&cubeMesh] {
        std::vector<Vertex> ret;
        for (size_t i = 0; i < cubeMesh.pos.size(); i++) {
            ret.push_back({cubeMesh.pos[i], cubeMesh.uvs[i]});
        }
        return ret;
    }();
    // constexpr auto indices = std::to_array<uint16_t>({
    //     0, 1, 2,
    //     2, 3, 0
    // });
    const auto [vertexBuffer, vertexBufferMemory] = createDeviceLocalBufferUnique(vk::BufferUsageFlagBits::eVertexBuffer, vertices);
    // const auto [indexBuffer, indexBufferMemory] = createDeviceLocalBufferUnique(vk::BufferUsageFlagBits::eIndexBuffer, indices);

    const auto textureFormat = vk::Format::eR8G8B8A8Srgb;
    const auto textureImage = createDeviceLocalTextureUnique("textures/bricks.png", textureFormat);
    const auto textureImageView = device->createImageViewUnique({
        .flags = {},
        .image = textureImage.first.get(),
        .viewType = vk::ImageViewType::e2D,
        .format = textureFormat,
        .components = {},
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    });
    const auto textureSampler = device->createSamplerUnique({
        .flags = {},
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .mipLodBias = 0,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = physicalDevice.getProperties().limits.maxSamplerAnisotropy,
        .compareEnable = VK_FALSE,
        .compareOp = {},
        .minLod = 0,
        .maxLod = 0,
        .borderColor = {},
        .unnormalizedCoordinates = false,
    });

    // Create command pool
    const auto commandPool = device->createCommandPoolUnique({
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = graphicsQueueFamily,
    });

    constexpr uint32_t maxFramesInFlight = 2;

    const auto descriptorPool = [&device] {
        const auto poolSizes = std::to_array<vk::DescriptorPoolSize>({
            {
                .type = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = maxFramesInFlight,
            }, {
                .type = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = maxFramesInFlight,
            },
        });
        return device->createDescriptorPoolUnique({
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = maxFramesInFlight,
            .poolSizeCount = poolSizes.size(),
            .pPoolSizes = poolSizes.data(),
        });
    }();

    struct FrameInFlight {
        vk::UniqueCommandBuffer commandBuffer;
        vk::UniqueSemaphore imageAvailableSemaphore;
        vk::UniqueSemaphore renderFinishedSemaphore;
        vk::UniqueFence inFlightFence;
        std::pair<vk::UniqueBuffer, vk::UniqueDeviceMemory> uniformBuffer;
        void* uniformBufferMapping;
        vk::UniqueDescriptorSet descriptorSet;
    };
    std::vector<FrameInFlight> framesInFlight;
    for (uint32_t i = 0; i < maxFramesInFlight; i++) {
        FrameInFlight f;
        f.commandBuffer = std::move(device->allocateCommandBuffersUnique({
            .commandPool = commandPool.get(),
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1,
        })[0]);
        f.imageAvailableSemaphore = device->createSemaphoreUnique({});
        f.renderFinishedSemaphore = device->createSemaphoreUnique({});
        f.inFlightFence = device->createFenceUnique({
            .flags = vk::FenceCreateFlagBits::eSignaled,
        });
        f.uniformBuffer = createBufferUnique(sizeof(UniformBuffer), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        f.uniformBufferMapping = nullptr;
        f.descriptorSet = std::move(device->allocateDescriptorSetsUnique({
            .descriptorPool = descriptorPool.get(),
            .descriptorSetCount = 1,
            .pSetLayouts = &descriptorSetLayout.get(),
        })[0]);
        f.uniformBufferMapping = device->mapMemory(f.uniformBuffer.second.get(), 0, sizeof(UniformBuffer), {});
        vk::DescriptorBufferInfo bufferInfo = {
            .buffer = f.uniformBuffer.first.get(),
            .offset = 0,
            .range = vk::WholeSize,
        };
        vk::DescriptorImageInfo imageInfo = {
            .sampler = textureSampler.get(),
            .imageView = textureImageView.get(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        };
        device->updateDescriptorSets({
            vk::WriteDescriptorSet {
                .dstSet = f.descriptorSet.get(),
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pImageInfo = nullptr,
                .pBufferInfo = &bufferInfo,
                .pTexelBufferView = nullptr,
            },
            vk::WriteDescriptorSet {
                .dstSet = f.descriptorSet.get(),
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &imageInfo,
                .pBufferInfo = nullptr,
                .pTexelBufferView = nullptr,
            },
        }, nullptr);
        framesInFlight.push_back(std::move(f));
    }

    bool framebufferResized = false;

    const auto recordCommandBuffer = [
        &renderPass,
        &graphicsPipeline,
        &pipelineLayout,
        &vertexBuffer,
        nVertices = vertices.size(),
        // &indexBuffer,
        // nIndices = indices.size(),
        &imageExtent = swapchainInfo.imageExtent,
        &swapchainFramebuffers
    ](const vk::UniqueCommandBuffer& commandBuffer, uint32_t imageIndex, const vk::UniqueDescriptorSet& descriptorSet) {
        vk::ClearValue clearColor = {
            .color = {
                .float32 = std::to_array<float>({0, 0, 0, 1})
            },
        };
        commandBuffer->begin({
            .flags = {},
            .pInheritanceInfo = nullptr,
        });
        commandBuffer->beginRenderPass({
            .renderPass = renderPass.get(),
            .framebuffer = swapchainFramebuffers[imageIndex].get(),
            .renderArea = {
                .offset = {0, 0},
                .extent = imageExtent,
            },
            .clearValueCount = 1,
            .pClearValues = &clearColor,
        }, vk::SubpassContents::eInline);
        commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline.get());
        commandBuffer->bindVertexBuffers(0, {vertexBuffer.get()}, {0});
        // commandBuffer->bindIndexBuffer(indexBuffer.get(), 0, vk::IndexType::eUint16);
        commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout.get(), 0, descriptorSet.get(), nullptr);
        commandBuffer->setViewport(0, vk::Viewport {
            .x = 0,
            .y = 0,
            .width = (float) imageExtent.width,
            .height = (float) imageExtent.height,
            .minDepth = 0,
            .maxDepth = 1,
        });
        commandBuffer->setScissor(0, vk::Rect2D {
            .offset = {0, 0},
            .extent = imageExtent,
        });
        commandBuffer->draw(nVertices, 1, 0, 0);
        // commandBuffer->drawIndexed(nIndices, 1, 0, 0, 0);
        commandBuffer->endRenderPass();
        commandBuffer->end();
    };

    auto drawFrame = [
        &device,
        &swapchain,
        &swapchainInfo,
        &recordCommandBuffer,
        &framesInFlight,
        &graphicsQueue,
        &presentQueue,
        frameIndex = 0,
        st = Stopwatch(),
        &framebufferResized // mutable
    ]() mutable { // Draw frame
        const auto& currentFrame = framesInFlight[frameIndex];
        (void) device->waitForFences(currentFrame.inFlightFence.get(), VK_TRUE, -1);
        auto [res, imageIndex] = device->acquireNextImageKHR(swapchain.get(), -1, currentFrame.imageAvailableSemaphore.get(), nullptr);
        if (res != vk::Result::eSuccess) {
            if (res == vk::Result::eSuboptimalKHR || res == vk::Result::eErrorOutOfDateKHR) {
                framebufferResized = true;
                return;
            } else {
                throw ex::fn("vkAcquireNextImageKHR()", (int) res);
            }
        }
        device->resetFences(currentFrame.inFlightFence.get());
        [&st, &swapchainInfo, &currentFrame] {
            float time = st.ping() / 1000;
            Transform transform = {
                .position = {0, 0, 0},
                .rotation = Quaternion::Euler({time/3, time/2, time}),
                .scale = Vector3(0.5),
            };
            SpaceCamera camera = {{
                .position = {0, -1, 0},
            }};
            Matrix4 model = transform.Matrix();
            Matrix4 view = Transform::z_convert * camera.Matrix().Inverse();
            float aspect = (float) swapchainInfo.imageExtent.width / swapchainInfo.imageExtent.height;
            Matrix4 proj = Transform::PerspectiveProjection(90, aspect);
            UniformBuffer ubo = {
                .MVP = (proj * view * model).Transposed(),
            };
            memcpy(currentFrame.uniformBufferMapping, &ubo, sizeof(ubo));
        }();
        recordCommandBuffer(currentFrame.commandBuffer, imageIndex, currentFrame.descriptorSet);
        constexpr vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
        graphicsQueue.submit(vk::SubmitInfo {
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &currentFrame.imageAvailableSemaphore.get(),
            .pWaitDstStageMask = waitStages,
            .commandBufferCount = 1,
            .pCommandBuffers = &currentFrame.commandBuffer.get(),
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &currentFrame.renderFinishedSemaphore.get(),
        }, currentFrame.inFlightFence.get());
        res = presentQueue.presentKHR(vk::PresentInfoKHR {
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &currentFrame.renderFinishedSemaphore.get(),
            .swapchainCount = 1,
            .pSwapchains = &swapchain.get(),
            .pImageIndices = &imageIndex,
            .pResults = nullptr,
        });
        if (res != vk::Result::eSuccess) {
            if (res == vk::Result::eSuboptimalKHR || res == vk::Result::eErrorOutOfDateKHR) {
                framebufferResized = true;
            } else {
                throw ex::fn("vkQueuePresentKHR()", (int) res);
            }
        }
        frameIndex = (frameIndex + 1) % maxFramesInFlight;
    };

    // Main loop
    [&window, &drawFrame, &framebufferResized, &device, &recreateSwapchainUnique] {
        FrameCounter frameCounter;
        Stopwatch benchStopwatch;
        while (!glfwWindowShouldClose(window.get())) {
            glfwPollEvents();
            drawFrame();
            frameCounter.tick();
            if (frameCounter.frameCount == 0) {
                const double t = benchStopwatch.ping() / 1000;
                const double fc = frameCounter.frameCount;
                prn_raw(t, " s total, ", t/fc*1000, " ms avg (", fc/t, " fps)");
                break;
            }
            if (framebufferResized) {
                prn("Framebuffer resized");
                { // Wait until window is not minimized
                    int w, h;
                    glfwGetFramebufferSize(window.get(), &w, &h);
                    while (w == 0 || h == 0) {
                        prn("Window is minimized, waiting...");
                        glfwWaitEvents();
                        glfwGetFramebufferSize(window.get(), &w, &h);
                    }
                }
                device->waitIdle();
                recreateSwapchainUnique();
                framebufferResized = false;
            }
        }
    }();
    // Wait for all frames to finish rendering
    std::vector<vk::Fence> inFlightFencesVec;
    for (const auto& e : framesInFlight) { inFlightFencesVec.push_back(e.inFlightFence.get()); }
    (void) device->waitForFences(inFlightFencesVec, VK_TRUE, -1);
    device->waitIdle(); // Unnecessary, but silences a validation layer bug
}
