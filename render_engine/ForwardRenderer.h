#pragma once
#include "vlk/GraphicsContext.h"
#include "vlk/ImageAttachment.h"
#include "vlk/WindowRenderTarget.h"
#include "vlk/utils.h"
#include "Mesh.h"
#include "Material.h"

// TODO too specific
inline vk::UniquePipeline makeGraphicsPipeline(
    const GraphicsContext* vlk,
    vk::PipelineLayout pipelineLayout,
    vk::RenderPass renderPass,
    uint32_t subpass
) {
    const auto vertShader = vlk->createShaderModule("shaders/triangle.vert.spv");
    const auto fragShader = vlk->createShaderModule("shaders/triangle.frag.spv");
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
        vlk->genShaderStageCreateInfo(vk::ShaderStageFlagBits::eVertex, vertShader),
        vlk->genShaderStageCreateInfo(vk::ShaderStageFlagBits::eFragment, fragShader),
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
        .rasterizationSamples = vlk->props.maxSampleCount,
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
    return vlk->device->createGraphicsPipelineUnique(nullptr, pipelineInfo).value;
}

class ForwardRenderer : public WindowRenderTarget<ForwardRenderer> {

    vk::Extent2D currentImageExtent;
    vk::Format currentImageFormat;
    std::span<const vk::ImageView> currentImageViews;

    vk::SampleCountFlagBits sampleCount;
    vk::UniqueRenderPass renderPass;
    // TODO rename FramebufferResources? / InternalResources
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

private:
    class CommandRecorder {
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

        vk::CommandBuffer commandBuffer;
        LazyUpdate<vk::Buffer> lastVertexBuffer;
        LazyUpdate<vk::Buffer> lastIndexBuffer;
        LazyUpdate<vk::Pipeline> lastPipeline;
        LazyUpdate<vk::DescriptorSet> lastMaterialDescriptorSet;

    public:
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

    std::optional<CommandRecorder> commandRecorder;

private:
    void createRenderPass() {
        vk::AttachmentDescription colorAttachmentDesc = {
            .flags = {},
            .format = currentImageFormat,
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
            .format = currentImageFormat,
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
            .format = currentImageFormat,
            .extent = {
                currentImageExtent.width,
                currentImageExtent.height,
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
                .width = currentImageExtent.width,
                .height = currentImageExtent.height,
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
        for (const auto& resolveImageView : currentImageViews) {
            const auto attachments = std::to_array({
                swapchainResources.colorAttachment.imageView.get(),
                swapchainResources.depthAttachment.imageView.get(),
                resolveImageView,
            });
            swapchainResources.framebuffers.push_back(vlk->device->createFramebufferUnique({
                .flags = {},
                .renderPass = renderPass.get(),
                .attachmentCount = attachments.size(),
                .pAttachments = attachments.data(),
                .width = currentImageExtent.width,
                .height = currentImageExtent.height,
                .layers = 1,
            }));
        }
    }

protected:
    friend WindowRenderTarget;
    void notifySetRenderTarget(
        vk::Extent2D extent,
        vk::Format format,
        std::span<const vk::ImageView> imageViews
    ) {
        currentImageExtent = extent;
        currentImageFormat = format;
        currentImageViews = imageViews;
        createRenderPass();
        createSwapchainResources();
    }
    void notifyUpdateImageExtent(vk::Extent2D extent) {
        currentImageExtent = extent;
        createSwapchainResources();
    }
    void notifyUpdateImageFormat(vk::Format format) {
        currentImageFormat = format;
        createRenderPass();
        createSwapchainResources();
    }

    void notifyStartFrame(vk::CommandBuffer commandBuffer, size_t imageIndex) {
        commandRecorder = CommandRecorder {
            commandBuffer,
            renderPass.get(),
            swapchainResources.framebuffers[imageIndex].get(),
            currentImageExtent,
        };
    }

    void notifyEndFrame() {
        commandRecorder.value().end();
    }

public:
    explicit ForwardRenderer(const GraphicsContext* vlk, const WindowSurface* window) : WindowRenderTarget(vlk, window) {
        sampleCount = vlk->props.maxSampleCount;
        // TODO this is ugly
        init();
    }

    void registerMaterialType(vk::DescriptorSetLayout descriptorSetLayout) {
        auto pipelineLayout = createPipelineLayout(
            vlk,
            std::to_array({descriptorSetLayout}),
            std::to_array({
                vk::PushConstantRange {
                    .stageFlags = vk::ShaderStageFlagBits::eVertex,
                    .offset = 0,
                    .size = sizeof(Matrix4),
                }
            })
        );
        auto pipeline = makeGraphicsPipeline(vlk, pipelineLayout.get(), renderPass.get(), 0);
        registeredMaterials.emplace(descriptorSetLayout, RegisteredMaterialType {
            .pipelineLayout = std::move(pipelineLayout),
            .pipeline = std::move(pipeline),
        });
    }

    void draw(const Mesh& mesh, const Material& material, const Matrix4& mvp) {
        const auto& registeredMaterial = registeredMaterials.at(material.descriptorSetLayout);
        commandRecorder.value().draw(mesh, registeredMaterial.pipeline.get(), registeredMaterial.pipelineLayout.get(), material, mvp);
    }
};
