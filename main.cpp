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
// #include <Stopwatch.h>

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
                device.getSurfacePresentModesKHR(surface).size() > 0;
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
        const vk::PhysicalDeviceFeatures usedFeatures {};
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
        Vector2 pos;
        Vector3 color;
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
            .format = vk::Format::eR32G32Sfloat,
            .offset = offsetof(Vertex, pos),
        }, {
            .location = 1,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = offsetof(Vertex, color),
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
    const auto createImageViews = [&device](const vk::SwapchainKHR& swapchain, vk::Format format) {
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
    const auto createRenderPass = [&device](vk::Format format) {
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
    const auto createFramebuffers = [&device](std::span<const vk::UniqueImageView> imageViews, const vk::Extent2D& extent, const vk::RenderPass& renderPass) {
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
    const auto createGraphicsPipeline = [&device, &bindingDescription, &attributeDescriptions](
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

    // Create pipeline layout
    const auto pipelineLayout = [&device] {
        vk::PipelineLayoutCreateInfo createInfo = {
            .flags = {},
            .setLayoutCount = 0,
            .pSetLayouts = nullptr,
            .pushConstantRangeCount = 0,
            .pPushConstantRanges = nullptr,
        };
        return device->createPipelineLayoutUnique(createInfo);
    }();

    const auto createShaderModule = [&device](std::string_view code) {
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
    const auto vertShader = createShaderModule(slurp("shaders/triangle.vert.spv"));
    const auto fragShader = createShaderModule(slurp("shaders/triangle.frag.spv"));

    vk::SwapchainCreateInfoKHR         swapchainInfo;
    vk::UniqueSwapchainKHR             swapchain;
    std::vector<vk::UniqueImageView>   swapchainImageViews;
    vk::UniqueRenderPass               renderPass;
    std::vector<vk::UniqueFramebuffer> swapchainFramebuffers;
    vk::UniquePipeline                 graphicsPipeline;

    const auto recreateSwapchain = [&]() {
        swapchainInfo         = pickSwapchainSettings(swapchain.get());
        swapchain             = device->createSwapchainKHRUnique(swapchainInfo);
        swapchainImageViews   = createImageViews(swapchain.get(), swapchainInfo.imageFormat);
        renderPass            = createRenderPass(swapchainInfo.imageFormat);
        swapchainFramebuffers = createFramebuffers(swapchainImageViews, swapchainInfo.imageExtent, renderPass.get());
        graphicsPipeline      = createGraphicsPipeline(pipelineLayout.get(), renderPass.get(), vertShader.get(), fragShader.get());
    };
    recreateSwapchain();

    const auto createBuffer = [&physicalDevice, &device](size_t nBytes, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties) {
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
        vk::UniqueDeviceMemory deviceMemory = [&physicalDevice, &device, &buffer, &properties] {
            auto memoryRequirements = device->getBufferMemoryRequirements(buffer.get());

            const auto findMemoryType = [physicalDevice](uint32_t typeFilter, vk::MemoryPropertyFlags requiredProperties) {
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

    // Create a utility command pool
    const vk::UniqueCommandPool commandPoolUtil = [&device, &graphicsQueueFamily] {
        vk::CommandPoolCreateInfo createInfo = {
            .flags = vk::CommandPoolCreateFlagBits::eTransient,
            .queueFamilyIndex = graphicsQueueFamily,
        };
        return device->createCommandPoolUnique(createInfo);
    }();

    // const auto createVertexBuffer = [device, &createBuffer](tl::span<uint8_t> data) {
    //     auto ret = createBuffer(
    //         data.size(),
    //         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
    //         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    //     );
    //     auto [buffer, deviceMemory] = ret;
    //
    //     // Copy vertex data to the vertex buffer
    //     void* mapped;
    //     vkMapMemory(device, deviceMemory, 0, data.size(), 0, &mapped);
    //     memcpy(mapped, data.data(), data.size());
    //     vkUnmapMemory(device, deviceMemory);
    //     return ret;
    // };

    const auto createVertexBuffer = [
        &device,
        &createBuffer,
        &commandPoolUtil,
        graphicsQueue
    ](std::span<const std::byte> data) {
        auto stagingBuffer = createBuffer(
            data.size(),
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        auto vertexBuffer = createBuffer(
            data.size(),
            vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );

        // Copy vertex data to the staging buffer
        void* mapped = device->mapMemory(stagingBuffer.second.get(), 0, data.size(), {});
        memcpy(mapped, data.data(), data.size());
        device->unmapMemory(stagingBuffer.second.get());

        const auto copyBuffer = [&device, &commandPoolUtil, &queue = graphicsQueue](
            const vk::UniqueBuffer& src,
            const vk::UniqueBuffer& dst,
            const vk::DeviceSize& size
        ) {
            const auto commandBuffers = device->allocateCommandBuffersUnique({
                .commandPool = commandPoolUtil.get(),
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = 1,
            });
            commandBuffers[0]->begin({
                .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
                .pInheritanceInfo = 0,
            });
            commandBuffers[0]->copyBuffer(src.get(), dst.get(), vk::BufferCopy {
                .srcOffset = 0,
                .dstOffset = 0,
                .size = size,
            });
            commandBuffers[0]->end();
            queue.submit(vk::SubmitInfo {
                .waitSemaphoreCount = 0,
                .pWaitSemaphores = nullptr,
                .pWaitDstStageMask = nullptr,
                .commandBufferCount = 1,
                .pCommandBuffers = &commandBuffers[0].get(),
                .signalSemaphoreCount = 0,
                .pSignalSemaphores = nullptr,
            }, nullptr);
            queue.waitIdle();
        };
        copyBuffer(stagingBuffer.first, vertexBuffer.first, data.size());

        return vertexBuffer;
    };

    constexpr auto vertices = std::to_array<Vertex>({
        {{ 0.0f, -0.5f}, {1.0f, 1.0f, 0.0f}},
        {{ 0.5f,  0.5f}, {0.0f, 1.0f, 1.0f}},
        {{-0.5f,  0.5f}, {1.0f, 0.0f, 1.0f}},
    });
    const auto [vertexBuffer, vertexBufferMemory] = createVertexBuffer(std::as_bytes(std::span(vertices)));

    const auto recordCommandBuffer = [
        &renderPass,
        &graphicsPipeline,
        &vertexBuffer = vertexBuffer,
        nVertices = vertices.size(),
        &imageExtent = swapchainInfo.imageExtent,
        &swapchainFramebuffers
    ](const vk::UniqueCommandBuffer& commandBuffer, uint32_t imageIndex) {
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
        commandBuffer->endRenderPass();
        commandBuffer->end();
    };

    // Create command pool
    const auto commandPool = device->createCommandPoolUnique({
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = graphicsQueueFamily,
    });

    struct FrameInFlight {
        vk::UniqueCommandBuffer commandBuffer;
        vk::UniqueSemaphore imageAvailableSemaphore;
        vk::UniqueSemaphore renderFinishedSemaphore;
        vk::UniqueFence inFlightFence;
    };
    // constexpr uint32_t maxFramesInFlight = 2;
    std::vector<FrameInFlight> framesInFlight;
    for (uint32_t i = 0; i < 2; i++) {
        framesInFlight.push_back({
            .commandBuffer = std::move(device->allocateCommandBuffersUnique({
                .commandPool = commandPool.get(),
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = 1,
            })[0]),
            .imageAvailableSemaphore = device->createSemaphoreUnique({}),
            .renderFinishedSemaphore = device->createSemaphoreUnique({}),
            .inFlightFence = device->createFenceUnique({
                .flags = vk::FenceCreateFlagBits::eSignaled,
            }),
        });
    }
    bool framebufferResized = false;

    auto drawFrame = [
        &device,
        &swapchain,
        &recordCommandBuffer,
        &framesInFlight,
        &graphicsQueue,
        &presentQueue,
        frameIndex = 0,
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
        recordCommandBuffer(currentFrame.commandBuffer, imageIndex);
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
        frameIndex = (frameIndex + 1) % framesInFlight.size();
    };

    // Main loop
    [&window, &drawFrame, &framebufferResized, &device, &recreateSwapchain] {
        while (!glfwWindowShouldClose(window.get())) {
            glfwPollEvents();
            drawFrame();
            if (framebufferResized) {
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
                recreateSwapchain();
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
