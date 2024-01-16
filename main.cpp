#include "vlk/ImageAttachment.h"
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <cstddef>
// #include "vlk/MappedBuffer.h"
#include "vlk/GraphicsContext.h"
#include "vlk/TypedDescriptorPool.h"
#include "vlk/Texture.h"
#include "vlk/updateDescriptorSet.h"
#include <fmt.h>
#include <ex.h>
#include <Vector.h>
#include <Transform.h>
#include <Stopwatch.h>
// #include "primitives.h"
#include "FrameCounter.h"
// #include "input.h"
#include "load_obj.h"
// #include "physics.h"

// TODO move to GraphicsContext
vk::UniquePipelineLayout createPipelineLayout(
    GraphicsContext& vlk,
    std::span<const vk::DescriptorSetLayout> descriptorSetLayouts,
    std::span<const vk::PushConstantRange> pushConstantRanges
) {
    return vlk.device->createPipelineLayoutUnique({
        .flags = {},
        .setLayoutCount = (uint32_t) descriptorSetLayouts.size(),
        .pSetLayouts = descriptorSetLayouts.data(),
        .pushConstantRangeCount = (uint32_t) pushConstantRanges.size(),
        .pPushConstantRanges = pushConstantRanges.data(),
    });
}

// int main() {
//     GraphicsEngine gengine;
//     while (gengine.beginFrame()) {
//         gengine.endFrame();
//     }
// }

// TODO too specific
vk::UniquePipeline makeGraphicsPipeline(
    GraphicsContext& vlk,
    vk::PipelineLayout pipelineLayout,
    vk::RenderPass renderPass,
    uint32_t subpass
) {
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
    // TODO maxSampleCount
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
        .layout = pipelineLayout,
        .renderPass = renderPass,
        .subpass = subpass,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };
    return vlk.device->createGraphicsPipelineUnique(nullptr, pipelineInfo).value;
}

template <typename T>
class LazyUpdate {
    T lastValue;
public:
    constexpr explicit LazyUpdate(T initialValue = {}) : lastValue(std::move(initialValue)) {}
    bool update(T value) {
        if (lastValue != value) {
            lastValue = std::move(value);
            return true;
        }
        return false;
    }
};

struct Mesh {
    vk::Buffer vertexBuffer;
    vk::Buffer indexBuffer;
    size_t nVertices;
    size_t nIndices;
    bool indexed;
};
Mesh makeMesh(GraphicsContext* vlk, AssetPool& assets, std::string_view path) {
    const auto [vertices, indices] = load_obj(path);
    const auto vertexBuffer = std::get<vk::Buffer>(assets.storeTuple(vlk->createDeviceLocalBufferUnique(vk::BufferUsageFlagBits::eVertexBuffer, vertices)));
    const auto indexBuffer = std::get<vk::Buffer>(assets.storeTuple(vlk->createDeviceLocalBufferUnique(vk::BufferUsageFlagBits::eIndexBuffer, indices)));
    return Mesh {
        .vertexBuffer = vertexBuffer,
        .indexBuffer = indexBuffer,
        .nVertices = vertices.size(),
        .nIndices = indices.size(),
        .indexed = true,
    };
}

struct Material {
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;
};

struct MaterialType {
    TypedDescriptorPool descriptorPool;
    std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBindings;
    Material makeMaterial(std::span<const Texture> textures) const {
        auto descriptorSet = descriptorPool.alloc();
        updateDescriptorSet(descriptorPool.vlk, descriptorSet, descriptorSetLayoutBindings, textures);
        Material ret = {
            .descriptorSet = descriptorSet,
            .descriptorSetLayout = descriptorPool.descriptorSetLayout.get(),
        };
        return ret;
    }
};
inline auto makeMaterialType(GraphicsContext* vlk, std::span<const vk::DescriptorSetLayoutBinding> bindings) {
    return MaterialType {
        .descriptorPool = makeTypedDescriptorPool(*vlk, bindings, 1),
        .descriptorSetLayoutBindings = std::vector(bindings.begin(), bindings.end()),
    };
}

class ForwardRenderer {
    GraphicsContext* vlk;
    vk::SampleCountFlagBits sampleCount;
    vk::UniqueRenderPass renderPass;
    // TODO rename RenderPassResources?
    struct SwapchainResources {
        ImageAttachment colorAttachment;
        ImageAttachment depthAttachment;
        std::vector<vk::UniqueFramebuffer> framebuffers;
    } swapchainResources;

    struct RegisteredMaterialType {
        vk::UniquePipelineLayout pipelineLayout;
        vk::UniquePipeline pipeline;
    };
    std::map<vk::DescriptorSetLayout, RegisteredMaterialType> registeredMaterials;

    // TODO abstract away into a ring buffer or something similar
    size_t frameIndex = 0;

private:
    struct CommandRecorder {
        vk::CommandBuffer commandBuffer;
        LazyUpdate<vk::Buffer> lastVertexBuffer;
        LazyUpdate<vk::Buffer> lastIndexBuffer;
        LazyUpdate<vk::Pipeline> lastPipeline;
        LazyUpdate<vk::DescriptorSet> lastMaterialDescriptorSet;

        CommandRecorder(
            vk::CommandBuffer cmdBuffer,
            vk::RenderPass renderPass,
            vk::Framebuffer framebuffer,
            vk::Extent2D imageExtent
        ) : commandBuffer(cmdBuffer) {
            commandBuffer.begin({
                .flags = {},
                .pInheritanceInfo = nullptr,
            });
            constexpr auto clearValues = std::to_array({
                vk::ClearValue {
                    .color = {
                        .float32 = std::to_array<float>({0, 0, 0, 1})
                    },
                },
                vk::ClearValue {
                    .depthStencil = {
                        .depth = 1.0f,
                    },
                },
            });
            commandBuffer.beginRenderPass({
                .renderPass = renderPass,
                .framebuffer = framebuffer,
                .renderArea = {
                    .offset = {0, 0},
                    .extent = imageExtent,
                },
                .clearValueCount = (uint32_t) clearValues.size(),
                .pClearValues = clearValues.data(),
            }, vk::SubpassContents::eInline);
            commandBuffer.setViewport(0, vk::Viewport {
                .x = 0,
                .y = 0,
                .width = (float) imageExtent.width,
                .height = (float) imageExtent.height,
                .minDepth = 0,
                .maxDepth = 1,
            });
            commandBuffer.setScissor(0, vk::Rect2D {
                .offset = {0, 0},
                .extent = imageExtent,
            });
        }

        void draw(const Mesh& mesh, vk::Pipeline pipeline, vk::PipelineLayout pipelineLayout, const Material& material, const Matrix4& mvp) {
            if (lastVertexBuffer.update(mesh.vertexBuffer)) {
                commandBuffer.bindVertexBuffers(0, {mesh.vertexBuffer}, {0});
            }
            if (mesh.indexed && lastIndexBuffer.update(mesh.indexBuffer)) {
                commandBuffer.bindIndexBuffer(mesh.indexBuffer, 0, vk::IndexType::eUint32);
            }
            if (lastPipeline.update(pipeline)) {
                commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
            }
            if (lastMaterialDescriptorSet.update(material.descriptorSet)) {
                commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, material.descriptorSet, nullptr);
            }
            commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(Matrix4), &mvp);
            if (mesh.indexed) {
                commandBuffer.drawIndexed(mesh.nIndices, 1, 0, 0, 0);
            } else {
                commandBuffer.draw(mesh.nVertices, 1, 0, 0);
            }
        }

        void end() {
            commandBuffer.endRenderPass();
            commandBuffer.end();
        }
    };

private:
    void createRenderPass() {
        vk::AttachmentDescription colorAttachmentDesc = {
            .flags = {},
            .format = vlk->swapchain.info.imageFormat,
            .samples = sampleCount,
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
            .samples = sampleCount,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eDontCare,
            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
            .initialLayout = vk::ImageLayout::eUndefined,
            .finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        };
        vk::AttachmentDescription colorResolveDesc = {
            .flags = {},
            .format = vlk->swapchain.info.imageFormat,
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
        renderPass = vlk->device->createRenderPassUnique(createInfo);
    }

    void createSwapchainResources() {
        // TODO should be lazy allocated
        swapchainResources.colorAttachment = makeImageAttachment(vlk, {
            .flags = {},
            .imageType = vk::ImageType::e2D,
            .format = vlk->swapchain.info.imageFormat,
            .extent = {
                vlk->swapchain.info.imageExtent.width,
                vlk->swapchain.info.imageExtent.height,
                1
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = sampleCount,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eColorAttachment,
            .sharingMode = vk::SharingMode::eExclusive,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = vk::ImageLayout::eUndefined,
        }, vk::MemoryPropertyFlagBits::eDeviceLocal, vk::ImageAspectFlagBits::eColor);
        swapchainResources.depthAttachment = makeImageAttachment(vlk, {
            .flags = {},
            .imageType = vk::ImageType::e2D,
            .format = vk::Format::eD32Sfloat,
            .extent = {
                .width = vlk->swapchain.info.imageExtent.width,
                .height = vlk->swapchain.info.imageExtent.height,
                .depth = 1,
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = sampleCount,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
            .sharingMode = vk::SharingMode::eExclusive,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr, // For shared sharingMode
            .initialLayout = vk::ImageLayout::eUndefined,
        }, vk::MemoryPropertyFlagBits::eDeviceLocal, vk::ImageAspectFlagBits::eDepth);
        swapchainResources.framebuffers.clear();
        for (const auto& resolveImageView : vlk->swapchain.imageViews) {
            const auto attachments = std::to_array({
                swapchainResources.colorAttachment.imageView.get(),
                swapchainResources.depthAttachment.imageView.get(),
                resolveImageView.get(),
            });
            swapchainResources.framebuffers.push_back(vlk->device->createFramebufferUnique({
                .flags = {},
                .renderPass = renderPass.get(),
                .attachmentCount = attachments.size(),
                .pAttachments = attachments.data(),
                .width = vlk->swapchain.info.imageExtent.width,
                .height = vlk->swapchain.info.imageExtent.height,
                .layers = 1,
            }));
        }
    }

    void onWindowResized() {
        vlk->device->waitIdle();
        vlk->recreateSwapchainUnique();
        createSwapchainResources();
    }

public:
    void registerMaterialType(vk::DescriptorSetLayout descriptorSetLayout) {
        auto pipelineLayout = createPipelineLayout(
            *vlk,
            std::to_array({descriptorSetLayout}),
            std::to_array({
                vk::PushConstantRange {
                    .stageFlags = vk::ShaderStageFlagBits::eVertex,
                    .offset = 0,
                    .size = sizeof(Matrix4),
                }
            })
        );
        auto pipeline = makeGraphicsPipeline(*vlk, pipelineLayout.get(), renderPass.get(), 0);
        registeredMaterials.emplace(descriptorSetLayout, RegisteredMaterialType {
            .pipelineLayout = std::move(pipelineLayout),
            .pipeline = std::move(pipeline),
        });
    }

    void init(GraphicsContext* graphicsContext) {
        vlk = graphicsContext;
        sampleCount = vlk->props.maxSampleCount;
        createRenderPass();
        createSwapchainResources();
    }

    void doFrame(auto&& drawCallback) try {
        const auto& currentFrame = vlk->framesInFlight[frameIndex++ % vlk->maxFramesInFlight];
        (void) vlk->device->waitForFences(currentFrame.inFlightFence.get(), VK_TRUE, -1);
        const uint32_t imageIndex = vlk->device->acquireNextImageKHR(vlk->swapchain.swapchain.get(), -1, currentFrame.imageAvailableSemaphore.get(), nullptr).value;
        // vkAcquireNextImageKHR may return vk::Result::eSuboptimalKHR.
        // This is not an error, which means that
        // imageAvailableSemaphore was signaled,
        // so we can't return early without waiting on it
        vlk->device->resetFences(currentFrame.inFlightFence.get());

        CommandRecorder commandRecorder {
            currentFrame.commandBuffer.get(),
            renderPass.get(),
            swapchainResources.framebuffers[imageIndex].get(),
            vlk->swapchain.info.imageExtent
        };
        drawCallback([this, &commandRecorder](const Mesh& mesh, const Material& material, const Matrix4& mvp) mutable {
            const auto& registeredMaterial = registeredMaterials.at(material.descriptorSetLayout);
            commandRecorder.draw(mesh, registeredMaterial.pipeline.get(), registeredMaterial.pipelineLayout.get(), material, mvp);
        });
        commandRecorder.end();

        constexpr vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
        vlk->graphicsQueue.submit(vk::SubmitInfo {
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &currentFrame.imageAvailableSemaphore.get(),
            .pWaitDstStageMask = waitStages,
            .commandBufferCount = 1,
            .pCommandBuffers = &currentFrame.commandBuffer.get(),
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &currentFrame.renderFinishedSemaphore.get(),
        }, currentFrame.inFlightFence.get());
        const vk::Result presentRes = vlk->presentQueue.presentKHR(vk::PresentInfoKHR {
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &currentFrame.renderFinishedSemaphore.get(),
            .swapchainCount = 1,
            .pSwapchains = &vlk->swapchain.swapchain.get(),
            .pImageIndices = &imageIndex,
            .pResults = nullptr,
        });
        if (presentRes == vk::Result::eSuboptimalKHR) {
            onWindowResized();
        }
    } catch (const vk::OutOfDateKHRError& e) {
        // Can be thrown from vkAcquireNextImageKHR or vkQueuePresentKHR
        onWindowResized();
    }

};

constexpr auto unlitMaterialBindings = std::to_array({
    vk::DescriptorSetLayoutBinding {
        .binding = 0,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment,
        .pImmutableSamplers = nullptr,
    },
});

int main() {
    GraphicsContext graphicsContext = makeGraphicsContext();
    GraphicsContext* vlk = &graphicsContext;
    AssetPool assets;
    ForwardRenderer renderer;
    renderer.init(vlk);

    const auto unlitMaterial = makeMaterialType(vlk, unlitMaterialBindings);

    renderer.registerMaterialType(unlitMaterial.descriptorPool.descriptorSetLayout.get());
    const auto bricksTexture = makeTexture(vlk, assets, "textures/bricks.png", vk::Format::eR8G8B8A8Srgb);
    const auto bricksUnlitMaterial = unlitMaterial.makeMaterial(std::span(&bricksTexture, 1));

    const auto cubeMesh = makeMesh(vlk, assets, "models/cube.obj");

    std::vector<Transform> cubes;
    for (size_t i = 0; i < 10; i++) {
        cubes.push_back({
            .position = {i * 2.0f, 0, 0},
            .rotation = Quaternion::Identity(),
            .scale = Vector3(1),
        });
    }

    const Transform camera = {
        .position = {0, 2, 0},
        .rotation = Quaternion::Euler(0, 0, std::numbers::pi),
    };

    const auto drawFrame = [&]() {
        renderer.doFrame([&](auto&& draw) {
            for (const auto& transform : cubes) {
                const Matrix4 model = transform.Matrix();
                const Matrix4 view = Transform::z_convert * camera.Matrix().Inverse();
                const float aspect = (float) vlk->swapchain.info.imageExtent.width / vlk->swapchain.info.imageExtent.height;
                const Matrix4 proj = Transform::PerspectiveProjection(90, aspect, {0.1, 500}) * Transform::y_flip;
                // const Matrix4 proj = Transform::OrthgraphicProjection(2, aspect, {0.1, 10}) * Transform::y_flip;
                const Matrix4 mvp = (proj * view * model).Transposed();
                draw(cubeMesh, bricksUnlitMaterial, mvp);
            }
        });
    };


    const auto runWindowLoop = [](GLFWwindow* window, auto&& drawFrame) {
        for (size_t i = 0; i < 100; i++) {
            glfwPollEvents();
            drawFrame();
        }
        FrameCounter frameCounter;
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            drawFrame();
            frameCounter.tick();
            if (frameCounter.frameCount() == 0) {
                prn_raw(frameCounter.frameTimeTotal(), " s total, ", frameCounter.frameTimeAvg(), " ms avg (", frameCounter.fpsAvg(), " fps)");
                break;
            }
            // if (true) { // TODO test on wayland, maybe remove
            //     int w, h;
            //     glfwGetFramebufferSize(window, &w, &h);
            //     if (w == 0 || h == 0) {
            //         prn("Window is minimized, waiting...");
            //         while (w == 0 || h == 0) {
            //             glfwWaitEvents();
            //             glfwGetFramebufferSize(window, &w, &h);
            //         }
            //     }
            // }
        }
    };

    runWindowLoop(vlk->window.get(), drawFrame);
    vlk->device->waitIdle();
}

// void applySystem(const auto& fn, auto&... objectRanges) {
//     (std::ranges::for_each(objectRanges, fn), ...);
// }
//
// struct StaticObject {
//     Transform transform;
//     RenderablePool::Instance renderable;
// };
//
// struct Player {
//     Transform transform = {{0, 0, 0}};
//     Vector3 euler = Vector3(0);
//     Vector3 velocity = Vector3(0);
//     void update(float dt, GLFWwindow* window) {
//         constexpr float accel = 30;
//         constexpr float airAccel = 300;
//         constexpr float maxSpeed = 3.0;
//         constexpr float airMaxSpeed = 0.3;
//         // transform.rotation = Quaternion::Euler(euler += input::get_rotation(window) * 3 * dt);
//         transform.rotation = Quaternion::Euler(euler = getMousePos(window) * 0.001);
//         const Vector3 rawMove = input::get_move(window);
//         const Vector2 move = Vector2::Rotate(rawMove.xy().SafeNormalized(), euler.z) * dt;
//         const bool touchingGround = transform.position.z == 0;
//         const bool jumping = rawMove.z > 0;
//         const bool inAir = !touchingGround || jumping;
//         // Friction
//         if (!inAir) {
//             const Vector2 badVel = move ? Vector2::ProjectionOnPlane(velocity.xy(), move) : velocity.xy();
//             if (badVel) {
//                 const float friction = 10 * dt;
//                 velocity -= Vector3(badVel.ClampedMagnitude(friction), 0);
//             }
//         }
//         // Move
//         {
//             const float curSpeed = Vector2::ProjectionLength(velocity.xy(), move);
//             const float missingSpeed = std::max(0.0f, (inAir ? airMaxSpeed : maxSpeed) - curSpeed);
//             if (move) { velocity += Vector3((move * (inAir ? airAccel : accel)).ClampedMagnitude(missingSpeed), 0); }
//         }
//         // Gravity
//         velocity += Vector3(0, 0, -10 * dt);
//         // Jump
//         if (touchingGround && jumping) { velocity.z = 3.0; }
//         // Apply velocity
//         transform.position += velocity * dt;
//         // Floor collision
//         if (transform.position.z < 0) {
//             transform.position.z = 0;
//             velocity.z = 0;
//         }
//     }
//     static Vector3 getMousePos(GLFWwindow* window) {
//         double x, y;
//         glfwGetCursorPos(window, &x, &y);
//         return Vector3(-y, 0, -x);
//     }
// };
//
// class Scene1 : public Scene {
//     GraphicsContext* vlk;
//     AssetPool* assets;
//     RenderablePool renderablePool;
//     physics::CollisionGrid collisionGrid;
//
//     std::vector<StaticObject> sceneObjects;
//     std::vector<Player> players;
//     Pipeline graphicsPipeline;
//
// public:
//     Scene1(GraphicsContext* vlk, AssetPool* assets)
//         : vlk(vlk), assets(assets), renderablePool(vlk, 501) {}
//
//     void init() {
//         constexpr size_t nCubes = 500;
//         const auto brickMaterial = assets->makeUnlitMaterial("textures/bricks.png");
//         const auto whiteMaterial = assets->makeUnlitMaterial("textures/white.png");
//         const auto cubeMesh = assets->makeMesh("models/cube.obj");
//         const auto planeMesh = assets->makeMesh("models/plane.obj");
//         graphicsPipeline = renderablePool.makePipeline(brickMaterial, vlk->renderPass, 0);
//
//         for (size_t i = 0; i < nCubes; i++) {
//             sceneObjects.push_back({
//                 .transform = {
//                     .position = {i * 2.0f, 0, 0},
//                     .rotation = Quaternion::Identity(),
//                     .scale = Vector3(1),
//                 },
//                 .renderable = renderablePool.alloc(cubeMesh, brickMaterial),
//             });
//             collisionGrid.add(sceneObjects.back().transform.position.xy());
//         }
//         sceneObjects.push_back({
//             .transform = {
//                 .position = {0, 1, -0.5},
//                 .rotation = Quaternion::Identity(),
//                 .scale = Vector3(10),
//             },
//             .renderable = renderablePool.alloc(planeMesh, whiteMaterial),
//         });
//
//         players.push_back({
//             .transform = {{0, 2, 0}},
//             .euler = {0, 0, std::numbers::pi},
//         });
//
//     }
//
//     void frame(float time, float dt, size_t frameNumber, vk::CommandBuffer commandBuffer, vk::Framebuffer framebuffer) override {
//         (void) time;
//         (void) frameNumber;
//         applySystem([&](auto& player) {
//             player.update(dt, vlk->window.get());
//         }, players);
//         applySystem([&](auto& player) {
//             if (const auto cp = collisionGrid.checkCollision(player.transform.position, 0.20)) {
//                 physics::resolveCollision(player.transform.position, player.velocity, *cp);
//             }
//         }, players);
//         render(commandBuffer, framebuffer);
//     }
//
// private:
//     void render(vk::CommandBuffer commandBuffer, vk::Framebuffer framebuffer) {
//         applySystem([&](const auto& e) {
//             e.renderable.updateUbo(e.transform, players[0].transform, vlk->swapchain.info.imageExtent);
//         }, sceneObjects);
//         const auto clearColors = std::to_array<vk::ClearValue>({
//             {
//                 .color = {
//                     .float32 = std::to_array<float>({0, 0, 0, 1})
//                 },
//             }, {
//                 .depthStencil = {
//                     .depth = 1.0f,
//                 },
//             },
//         });
//         ForwardRenderPass renderPass = {
//             .renderPass = vlk->renderPass.get(),
//             .framebuffer = framebuffer,
//             .imageExtent = vlk->swapchain.info.imageExtent,
//             .graphicsPipeline = graphicsPipeline.pipeline.get(),
//             .pipelineLayout = graphicsPipeline.pipelineLayout.get(),
//             .clearValues = clearColors,
//         };
//         renderPass.begin(commandBuffer);
//         applySystem([&](const auto& e) {
//             renderPass.renderOne(commandBuffer, e.renderable);
//         }, sceneObjects);
//         renderPass.end(commandBuffer);
//     }
// };
//
// int main() {
//     auto vlk = makeGraphicsContext();
//     if (glfwRawMouseMotionSupported()) {
//         prn("RawMouseMotion");
//         glfwSetInputMode(vlk.window.get(), GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
//     }
//     glfwSetInputMode(vlk.window.get(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
//     AssetPool assets {&vlk};
//
//     Scene1 scene {&vlk, &assets};
//     scene.init();
//
//     Renderer renderer {&vlk, &scene};
//
//     runWindowLoop(vlk, renderer);
// }
//
// // Create render pass
// // vlk.recreateFramebuffersUnique(renderPass);
//
// // vlk.commandPool = vlk.device->createCommandPoolUnique({
// //     .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
// //     .queueFamilyIndex = vlk.props.graphicsQueueFamily,
// // });
