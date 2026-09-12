module;
#include <vulkan/vulkan.hpp>

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

module VulkanApp;

import Logger;

namespace {

constexpr int  WIN_WIDTH  = 1280;
constexpr int  WIN_HEIGHT = 720;
constexpr char WIN_TITLE[] = "PGR_VK";


static_assert(DEPTH_CLEAR == 0.0f,
              "CameraComponent::viewProjection swaps near and far, so the far "
              "plane is 0 and DEPTH_CLEAR must clear to it; VkUtil's "
              "DEPTH_COMPARE_OP has to be a GREATER one to match");

void check(vk::Result r, const char* what) {
    if (r != vk::Result::eSuccess)
        throw std::runtime_error(std::string(what) + " failed: " + vk::to_string(r));
}

} // namespace

VulkanApp::VulkanApp(uint32_t maxFrames) : maxFrames_(maxFrames) {}

VulkanApp::~VulkanApp() {
    /**
     * run() can throw: FrameRunner throws mid-frame, and a failed model
     * load throws before the loop starts.
     */
    cleanup();
    window_.shutdown();
}

void VulkanApp::init() {
    if (!window_.init(WIN_WIDTH, WIN_HEIGHT, WIN_TITLE))
        throw std::runtime_error("AppWindow::init failed");
    window_.setUIMode(true);

    ctx_.setDebugMessageSink(
        [this](const std::string& message) { shaderPrint_.push(message); });

    if (!ctx_.init(window_, WIN_TITLE))
        throw std::runtime_error("VulkanContext::init failed");
    device_ = ctx_.device();

    if (!swapchain_.init(ctx_, window_))
        throw std::runtime_error("Swapchain::init failed");

    if (!frames_.init(ctx_, swapchain_))
        throw std::runtime_error("FrameRunner::init failed");

    shaders_.init(device_);

    if (!renderer_.init(ctx_, shaders_, swapchain_.format(), swapchain_.extent()))
        throw std::runtime_error("Renderer::init failed");
    renderer_.setDrawCallback(
        [this](vk::CommandBuffer cmd, vk::Extent2D extent) { recordImGui(cmd, extent); });

    /* Swapchain::recreate has waited the device idle by the time this fires. */
    frames_.setSwapchainRecreatedCallback([this](vk::Extent2D extent, vk::Format) {
        if (!renderer_.resize(extent))
            throw std::runtime_error("Renderer::resize failed");
    });

    if (!buffers_.init(ctx_))
        throw std::runtime_error("BufferManager::init failed");
    if (!uploads_.init(ctx_, buffers_))
        throw std::runtime_error("UploadBatch::init failed");
    if (!loader_.init(buffers_, uploads_))
        throw std::runtime_error("GltfLoader::init failed");

    initImGuiVulkan();
}

void VulkanApp::run() {
    /* Whatever the caller put in the scene decides where the camera sits. */
    frameScene();

    window_.setResizeCallback([this](int, int) { frames_.notifyResized(); });
    window_.setUICallback([this] { drawUI(); });
    window_.setDrawCallback([this] {
        scene_.tick(elapseFrame());
        updateCamera();
        frames_.drawFrame([this](Frame::Recording& recording) {
            recordFrame(recording);
        });
        //Lost device is not recoverable
        if (frames_.deviceLost()) window_.requestClose();

        if (maxFrames_ != 0 && ++framesDrawn_ >= maxFrames_)
            window_.requestClose();
    });

    window_.mainLoop();
}

void VulkanApp::initImGuiVulkan() {
    /* One combined image sampler per texture; only the font atlas so far. */
    const vk::DescriptorPoolSize poolSize{ vk::DescriptorType::eCombinedImageSampler, 16 };

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets       = 16;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    check(device_.createDescriptorPool(&poolInfo, nullptr, &imguiPool_),
          "vkCreateDescriptorPool (ImGui)");

    const vk::Format colorFormat = swapchain_.format();
    vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.colorAttachmentCount    = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    /**
     * ImGui does not depth test, but its pipeline must declare the same
     * attachments as the vk::RenderingInfo it is recorded into.
     */
    renderingInfo.depthAttachmentFormat   = DEPTH_FORMAT;

    ImGui_ImplVulkan_InitInfo info{};
    info.Instance        = static_cast<VkInstance>(ctx_.instance());
    info.PhysicalDevice  = static_cast<VkPhysicalDevice>(ctx_.physicalDevice());
    info.Device          = static_cast<VkDevice>(device_);
    info.QueueFamily     = ctx_.graphicsQueueFamily();
    info.Queue           = static_cast<VkQueue>(ctx_.graphicsQueue());
    info.DescriptorPool  = static_cast<VkDescriptorPool>(imguiPool_);
    info.MinImageCount   = swapchain_.minImageCount();
    info.ImageCount      = swapchain_.imageCount();
    info.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;
    info.UseDynamicRendering       = true;
    info.PipelineRenderingCreateInfo = static_cast<VkPipelineRenderingCreateInfo>(renderingInfo);
    info.CheckVkResultFn = [](VkResult r) { check(static_cast<vk::Result>(r), "ImGui Vulkan backend"); };

    if (!ImGui_ImplVulkan_Init(&info))
        throw std::runtime_error("ImGui_ImplVulkan_Init failed");
    imguiVulkanInitialized_ = true;
}

void VulkanApp::drawUI() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("pgr-vk")) {
        ImGui::Text("GPU: %s", ctx_.gpuName().c_str());
        ImGui::Text("%d x %d", window_.getWidth(), window_.getHeight());
        ImGui::Text("%.1f FPS (%.2f ms)", ImGui::GetIO().Framerate,
                    1000.0f / ImGui::GetIO().Framerate);
        ImGui::Separator();
        ImGui::TextUnformatted(window_.isUIMode() ? "UI mode  (Tab to capture the mouse)"
                                                  : "Mouse captured  (Tab for UI)");
        ImGui::TextUnformatted("Esc quits");
    }
    ImGui::End();

    shaderPrint_.drawUI();
}

void VulkanApp::frameScene() {
    glm::vec3 min{ std::numeric_limits<float>::max() };
    glm::vec3 max{ std::numeric_limits<float>::lowest() };

    for (const DrawItem& item : scene_.getDrawList().getItems()) {
        for (const MultiMeshPart& part : item.multiMesh->parts()) {
            const glm::mat4   world  = item.transform * part.localTransform;
            const MeshBounds& bounds = part.mesh->bounds();
            const glm::vec3 center =
                glm::vec3(world * glm::vec4(bounds.center, 1.0f));
            /* Largest axis scale; only used to place a camera. */
            const float scale = std::max({
                glm::length(glm::vec3(world[0])),
                glm::length(glm::vec3(world[1])),
                glm::length(glm::vec3(world[2])) });
            const float radius = bounds.radius * scale;

            min = glm::min(min, center - radius);
            max = glm::max(max, center + radius);
        }
    }

    if (min.x > max.x) {
        sceneCenter_ = glm::vec3{0.0f};
        sceneRadius_ = 1.0f;
    } else {
        sceneCenter_ = (min + max) * 0.5f;
        sceneRadius_ = std::max(glm::length(max - min) * 0.5f, 1e-3f);
    }

    if (!scene_.getActiveCamera()) {
        auto camera = std::make_unique<Object>();
        camera->addComponent(CameraComponent{});
        scene_.addObject(std::move(camera));
    }

    updateCamera();
}

float VulkanApp::elapseFrame() {
    const int now = window_.getElapsedMs();
    const int delta = now - lastFrameMs_;
    lastFrameMs_ = now;


    if (delta <= 0 || delta > MAX_FRAME_MS) return 0.0f;
    return static_cast<float>(delta) * 0.001f;
}

void VulkanApp::updateCamera() {
    CameraComponent* camera = scene_.getActiveCamera();
    if (!camera) return;

    const float angle    = static_cast<float>(window_.getElapsedMs()) * 0.0004f;
    const float distance = sceneRadius_ * 2.5f;
    const glm::vec3 eye = sceneCenter_ + distance * glm::vec3{
        std::cos(angle), 0.45f, std::sin(angle) };

    /* The Object's transform is where the camera is, not what it looks like
     * from there, so the view matrix lookAt builds is inverted back out. */
    camera->getOwner().setTransform(glm::inverse(
        glm::lookAt(eye, sceneCenter_, glm::vec3{0.0f, 1.0f, 0.0f})));
}

std::vector<GPUMeshInstance> VulkanApp::collectMeshInstances() {
    std::vector<GPUMeshInstance> instances;
    for (const DrawItem& item : scene_.getDrawList().getItems()) {
        if (!item.isVisible) continue;

        for (const MultiMeshPart& part : item.multiMesh->parts()) {
            const Mesh& mesh = *part.mesh;
            if (!mesh.uploaded() || mesh.meshletCount() == 0) continue;

            GPUMeshInstance record{};
            record.transform = item.transform * part.localTransform;
            record.mesh      = mesh.header();
            instances.push_back(record);
        }
    }
    return instances;
}

void VulkanApp::recordFrame(Frame::Recording& recording) {
    const CameraComponent* camera = scene_.getActiveCamera();
    const GpuPtr<GPUMaterial> materials =
        buffers_.staticBase<StaticBufferKind::Materials>();
    if (!camera) {
        renderer_.render(recording, {}, glm::mat4{1.0f}, materials);
        return;
    }

    const std::vector<GPUMeshInstance> instances = collectMeshInstances();
    const vk::Extent2D extent = recording.extent();
    const float aspect = static_cast<float>(extent.width) /
                         static_cast<float>(std::max(extent.height, 1u));
    renderer_.render(recording, instances, camera->viewProjection(aspect), materials);
}

void VulkanApp::recordImGui(vk::CommandBuffer cmd, vk::Extent2D) {
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(cmd));
}

void VulkanApp::cleanup() {
    /* Reachable from the destructor after a failed init. */
    if (!device_) return;

    ctx_.waitIdle();

    if (imguiVulkanInitialized_) {
        ImGui_ImplVulkan_Shutdown();
        imguiVulkanInitialized_ = false;
    }
    if (imguiPool_) {
        device_.destroyDescriptorPool(imguiPool_);
        imguiPool_ = nullptr;
    }

    /* Meshes retire buffer ranges on destruction; must precede shutdown. */
    scene_.clearObjects();
    renderer_.destroy();
    uploads_.destroy();
    buffers_.shutdown();
    frames_.destroy();

    swapchain_.destroy();
    ctx_.shutdown();
}
