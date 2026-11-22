module;
#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

module VulkanApp;

import vulkan;
import RendererConfig;
import Logger;
import DebugFlags;

namespace {

constexpr char APP_NAME[] = "VK";

static_assert(DEPTH_CLEAR == 0.0f,
              "CameraComponent::viewProjection swaps near and far, so the far "
              "plane is 0 and DEPTH_CLEAR must clear to it; VkUtil's "
              "DEPTH_COMPARE_OP has to be a GREATER one to match");

} // namespace

VulkanApp::~VulkanApp() {
    cleanup();
}

void VulkanApp::init(PresentationBridge& bridge, vk::Extent2D extent,
                     const std::filesystem::path& dataRoot,
                     std::span<const uint8_t> preferredDeviceUuid) {
    bridge_ = &bridge;

    if (!ctx_.init(APP_NAME, preferredDeviceUuid))
        throw std::runtime_error("VulkanContext::init failed");
    device_ = ctx_.device();

    if (!target_.init(ctx_, bridge, extent))
        throw std::runtime_error("ExternalTarget::init failed");

    if (!frames_.init(ctx_, target_))
        throw std::runtime_error("FrameRunner::init failed");

    if (!shaders_.init(device_, dataRoot / "Shaders"))
        throw std::runtime_error("ShaderLoader::init failed");

    /* Before the Renderer: the mesh pipeline layout names its set layout. */
    if (!textures_.init(ctx_, releaser_))
        throw std::runtime_error("TextureManager::init failed");

    if (!renderer_.init(ctx_, shaders_, target_.format(), target_.extent(),
                        textures_))
        throw std::runtime_error("Renderer::init failed");

    frames_.setTargetRecreatedCallback([this](vk::Extent2D extent, vk::Format) {
        if (!renderer_.resize(extent))
            throw std::runtime_error("Renderer::resize failed");
    });

    if (!buffers_.init(ctx_, releaser_))
        throw std::runtime_error("BufferManager::init failed");
    if (!transfers_.init(ctx_))
        throw std::runtime_error("BlockingTransferBatch::init failed");
    if (!loader_.init(buffers_, transfers_, textures_))
        throw std::runtime_error("ModelLoader::init failed");
}

void VulkanApp::run() {
    //Input presentation layer forwards
    std::vector<KeyReport> keys;
    while (!bridge_->stopRequested() && !frames_.deviceLost()) {
        releaser_.setLastSubmittedFrameSerial(frames_.submittedSerial());
        releaser_.releaseCompleted(frames_.getCompletedSerial());

        MouseDelta mouse{};
        bridge_->takeInput(keys, mouse);
        for (const KeyReport& key : keys) input_.pushKey(key.ascii, key.down);
        if (mouse.dx != 0.0f || mouse.dy != 0.0f) input_.pushMouseMove(mouse.dx, mouse.dy);
        keys.clear();

        dispatchInput();
        scene_.tick(elapseFrame());
        frames_.drawFrame([this](Frame::Recording& recording) {
            recordFrame(recording);
        });
    }
    if (frames_.deviceLost())
        bridge_->notifyError("device lost");
}

void VulkanApp::dispatchInput() {
    for (const InputEventData& event : input_.pending()) scene_.input(event);
    input_.clearPending();
}

float VulkanApp::elapseFrame() {
    static const auto start = std::chrono::steady_clock::now();
    const int now = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count());
    const int delta = now - lastFrameMs_;
    lastFrameMs_ = now;

    if (delta <= 0 || delta > MAX_FRAME_MS) return 0.0f;
    return static_cast<float>(delta) * 0.001f;
}

void VulkanApp::recordFrame(Frame::Recording& recording) {
    DrawList& drawList = scene_.getDrawList();

    /* Taken before the no-camera path returns: Renderer still has to hand it
     * to the frame-in-flight slots, or the writes are lost. */
    const std::span<const uint32_t> changed = drawList.takeChangedIndices();

    const CameraComponent* camera = scene_.getActiveCamera();
    const GpuPtr<GPUMaterial> materials =
        buffers_.staticBase<StaticBufferKind::Materials>();
    if (!camera) {
        renderer_.render(recording, {}, changed, glm::mat4{1.0f}, glm::vec3{0.0f},
                         materials);
        return;
    }

    const vk::Extent2D extent = recording.extent();
    const float aspect = static_cast<float>(extent.width) /
                         static_cast<float>(std::max(extent.height, 1u));
    renderer_.render(recording, drawList.getItems(), changed,
                     camera->viewProjection(aspect), camera->worldPosition(),
                     materials);
}

void VulkanApp::cleanup() {
    if (!device_) return;

    ctx_.waitIdle();

    /* Meshes and render targets retire ranges and slots on destruction; must precede releaseAll. */
    scene_.clearObjects();
    renderer_.destroy();
    releaser_.releaseAll();
    textures_.destroy();
    transfers_.destroy();
    buffers_.shutdown();
    frames_.destroy();

    target_.destroy();
    ctx_.shutdown();
}
