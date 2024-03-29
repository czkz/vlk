#include <cstddef>
#include <fmt.h>
#include <Transform.h>
#include "FrameCounter.h"
#include "render_engine/ForwardRenderer.h"
#include "vlk/WindowRenderTarget.h"

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
    glfwInit();
    const vk::UniqueInstance instance = createInstance();
    const WindowSurface window = createWindowSurface(instance.get());
    const GraphicsContext graphicsContext = makeGraphicsContext(instance.get(), window.surface.get());
    const GraphicsContext* vlk = &graphicsContext;
    AssetPool assets;
    WindowRenderTarget renderTarget (vlk, &window);
    ForwardRenderer renderer (vlk);
    renderer.setRenderTarget(renderTarget.renderTarget());
    renderTarget.onRecreateSwapchain = [&]() { renderer.updateRenderTarget(renderTarget.renderTarget()); };

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

    FrameCounter frameCounter;
    while (!glfwWindowShouldClose(window.window.get())) {
        glfwPollEvents();
        if (const auto frame = renderTarget.startFrame()) {
            renderer.startFrame(*frame);
            for (const auto& transform : cubes) {
                const Matrix4 model = transform.Matrix();
                const Matrix4 view = Transform::z_convert * camera.Matrix().Inverse();
                const float aspect = (float) renderer.getRenderTarget().extent.width / renderer.getRenderTarget().extent.height;
                const Matrix4 proj = Transform::PerspectiveProjection(90, aspect, {0.1, 500}) * Transform::y_flip;
                // const Matrix4 proj = Transform::OrthgraphicProjection(2, aspect, {0.1, 10}) * Transform::y_flip;
                const Matrix4 mvp = (proj * view * model).Transposed();
                renderer.draw(cubeMesh, bricksUnlitMaterial, mvp);
            }
            renderer.endFrame();
            renderTarget.endFrame();
        }
        frameCounter.tick();
        if (frameCounter.frameCount() == 0) {
            prn_raw(frameCounter.frameTimeTotal(), " s total, ", frameCounter.frameTimeAvg(), " ms avg (", frameCounter.fpsAvg(), " fps)");
            break;
        }
    }
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
