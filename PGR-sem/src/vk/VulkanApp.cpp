module;
#include <vulkan/vulkan.hpp>

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
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

constexpr char SHADER_DIR[] = "Shaders";


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

    if (!renderer_.init(ctx_, swapchain_.extent()))
        throw std::runtime_error("Renderer::init failed");
    renderer_.setDrawCallback(
        [this](vk::CommandBuffer cmd, vk::Extent2D extent) { recordDraw(cmd, extent); });

    /* Swapchain::recreate has waited the device idle by the time this fires. */
    frames_.setSwapchainRecreatedCallback([this](vk::Extent2D extent, vk::Format) {
        if (!renderer_.resize(extent))
            throw std::runtime_error("Renderer::resize failed");
    });

    shaders_.init(device_);

    if (!buffers_.init(ctx_))
        throw std::runtime_error("BufferManager::init failed");
    if (!uploads_.init(ctx_, buffers_))
        throw std::runtime_error("UploadBatch::init failed");
    if (!loader_.init(buffers_, uploads_))
        throw std::runtime_error("GltfLoader::init failed");

    if (!buildDrawCommands_.init(
            device_, shaders_,
            std::filesystem::path(SHADER_DIR) / "build_draw_commands.spv"))
        throw std::runtime_error("BuildDrawCommands::init failed");

    if (!meshDraw_.init(device_, shaders_,
                        std::filesystem::path(SHADER_DIR) / "mesh.spv",
                        std::filesystem::path(SHADER_DIR) / "mesh_frag.spv",
                        swapchain_.format(), DEPTH_FORMAT,
                        ctx_.cmdDrawMeshTasksIndirectCount()))
        throw std::runtime_error("MeshDraw::init failed");
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

VulkanApp::FrameSpans VulkanApp::allocateFrameSpans(
    FrameInFlightIndex frameInFlight, uint32_t instanceCount) {
    return {
        buffers_.allocateFrame<FrameSlotBufferKind::MeshInstances>(frameInFlight, instanceCount),
        buffers_.allocateFrame<FrameSlotBufferKind::DrawData>(frameInFlight, instanceCount),
        buffers_.allocateFrame<FrameSlotBufferKind::MeshTaskCommands>(frameInFlight,
                                                                  instanceCount),
        /* One counter, and vkCmdFillBuffer needs a 4-byte aligned offset. */
        buffers_.allocateFrame<FrameSlotBufferKind::MeshTaskCommandCount>(frameInFlight, 1, 4),
    };
}

void VulkanApp::recordBuildDrawCommands(vk::CommandBuffer cmd,
                                        const FrameSpans& spans,
                                        const glm::mat4& viewProj,
                                        uint32_t instanceCount) const {
    zero(cmd, spans.count.region);

    /* The counter is the target of the shader's atomicAdd. */
    barrier(cmd,
            vk::PipelineStageFlagBits2::eClear, vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageRead |
                vk::AccessFlagBits2::eShaderStorageWrite);

    BuildDrawCommandsPush push{};
    push.viewProj     = viewProj;
    push.instances = spans.instances.gpu.data;
    push.drawData     = spans.drawData.gpu.data;
    push.commands     = spans.commands.gpu.data;
    push.commandCount = spans.count.gpu.data;
    push.instanceCount  = instanceCount;

    buildDrawCommands_.record(cmd, push);

    /**
     * The commands and the count are fetched by the indirect draw itself;
     * the mesh shader reads drawData as storage, which is a separate
     * hazard from the command fetch.
     */
    barrier(cmd,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eDrawIndirect |
                vk::PipelineStageFlagBits2::eMeshShaderEXT,
            vk::AccessFlagBits2::eIndirectCommandRead |
                vk::AccessFlagBits2::eShaderStorageRead);
}

GPUMeshDrawPush VulkanApp::makeMeshDrawPush(const FrameSpans& spans,
                                            const glm::mat4& viewProj) const {
    GPUMeshDrawPush push{};
    push.viewProj  = viewProj;
    push.drawData  = spans.drawData.gpu.data;
    push.instances = spans.instances.gpu.data;
    push.materials = buffers_.staticBase<StaticBufferKind::Materials>();
    return push;
}

void VulkanApp::buildDrawCommands(Frame::Recording& recording) {
    const vk::CommandBuffer cmd = recording.commandBuffer();
    const vk::Extent2D extent = recording.extent();
    const FrameInFlightIndex frameInFlight = recording.frameInFlight();
    buffers_.resetFrame(frameInFlight);
    pendingDraw_ = {};

    /* No fallback matrix: an identity one would render as a rendering bug
     * instead of as a missing camera. */
    const CameraComponent* camera = scene_.getActiveCamera();
    if (!camera) return;

    const std::vector<GPUMeshInstance> instances = collectMeshInstances();
    if (instances.empty()) return;

    const auto instanceCount = static_cast<uint32_t>(instances.size());
    const FrameSpans spans = allocateFrameSpans(frameInFlight, instanceCount);
    if (!spans) {
        logError("buildDrawCommands: out of per-frame buffer space");
        return;
    }

    std::memcpy(spans.instances.host, instances.data(),
                instances.size() * sizeof(GPUMeshInstance));

    const float aspect = static_cast<float>(extent.width) /
                         static_cast<float>(std::max(extent.height, 1u));
    const glm::mat4 viewProj = camera->viewProjection(aspect);
    recordBuildDrawCommands(cmd, spans, viewProj, instanceCount);

    pendingDraw_.commands    = spans.commands.region;
    pendingDraw_.count       = spans.count.region;
    pendingDraw_.instanceCount = instanceCount;
    pendingDraw_.push        = makeMeshDrawPush(spans, viewProj);
}

void VulkanApp::recordFrame(Frame::Recording& recording) {
    buildDrawCommands(recording);
    renderer_.render(recording);
}

void VulkanApp::recordDraw(vk::CommandBuffer cmd, vk::Extent2D extent) {
    meshDraw_.record(cmd, extent, pendingDraw_.push,
                     pendingDraw_.commands, pendingDraw_.count,
                     pendingDraw_.instanceCount);

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
    meshDraw_.destroy();
    buildDrawCommands_.destroy();
    uploads_.destroy();
    buffers_.shutdown();

    renderer_.destroy();
    frames_.destroy();

    swapchain_.destroy();
    ctx_.shutdown();
}
