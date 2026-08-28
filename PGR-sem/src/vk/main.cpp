#include <vulkan/vulkan.h>

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

import VkWindow;
import VulkanContext;
import VkSwapchain;
import FrameRunner;
import VkUtil;
import ShadersLoader;
import VK_Buffers;
import UploadBatch;
import GltfLoader;
import BuildDrawCommands;
import MeshDraw;
import GPUTypes;
import ShaderPrint;
import Logger;

namespace {

constexpr int  WIN_WIDTH  = 1280;
constexpr int  WIN_HEIGHT = 720;
constexpr char WIN_TITLE[] = "PGR_VK";

void check(VkResult r, const char* what) {
    if (r != VK_SUCCESS)
        throw std::runtime_error(std::string(what) + " failed: VkResult " + std::to_string(r));
}

constexpr char SHADER_DIR[] = "Shaders";

class VulkanApp {
public:
    /**
     * @param modelPath .gltf/.glb to load, or empty for an empty scene.
     * @param maxFrames close after this many frames, or 0 to run until closed.
     *        A frame budget lets a script wait for a clean exit, which flushes
     *        stdout; killing the process loses it.
     */
    VulkanApp(std::filesystem::path modelPath, uint32_t maxFrames)
        : modelPath_(std::move(modelPath)), maxFrames_(maxFrames) {}

    void run() {
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

        if (!buffers_.init(ctx_))
            throw std::runtime_error("VK_buffers::init failed");
        if (!uploads_.init(ctx_, buffers_))
            throw std::runtime_error("UploadBatch::init failed");

        loadModel();

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

        window_.setResizeCallback([this](int, int) { frames_.notifyResized(); });
        window_.setUICallback([this] { drawUI(); });
        window_.setDrawCallback([this] {
            frames_.drawFrame([this](VkCommandBuffer cmd, const RenderTarget& target) {
                recordFrame(cmd, target);
            });
            //Lost device is not recoverable
            if (frames_.deviceLost()) window_.requestClose();

            if (maxFrames_ != 0 && ++framesDrawn_ >= maxFrames_)
                window_.requestClose();
        });

        window_.mainLoop();
    }

    ~VulkanApp() {
        /**
         * run() can throw: FrameRunner throws mid-frame, and a failed model
         * load throws before the loop starts.
         */
        cleanup();
        window_.shutdown();
    }

private:
    AppWindow     window_;
    VulkanContext ctx_;
    Swapchain     swapchain_;
    FrameRunner   frames_;
    ShaderLoader  shaders_;
    VK_buffers    buffers_;
    UploadBatch   uploads_;
    BuildDrawCommands buildDrawCommands_;
    MeshDraw          meshDraw_;
    ShaderPrint       shaderPrint_;

    /**
     * What buildDrawCommands() produced this frame, consumed by the indirect
     * draw in recordFrame(). objectCount 0 means there is nothing to draw.
     */
    struct PendingDraw {
        BufferSlice  commands{};
        BufferSlice  count{};
        MeshDrawPush push{};
        uint32_t     objectCount = 0;
    };
    PendingDraw pendingDraw_;

    std::filesystem::path  modelPath_;
    uint32_t               maxFrames_   = 0;
    uint32_t               framesDrawn_ = 0;
    std::vector<MultiMesh> scene_;
    glm::vec3              sceneCenter_{0.0f};
    float                  sceneRadius_ = 1.0f;

    VkDevice device_ = VK_NULL_HANDLE;

    VkDescriptorPool imguiPool_ = VK_NULL_HANDLE;
    bool             imguiVulkanInitialized_ = false;

    /**
     * Renderer half of the ImGui setup; AppWindow already created the context
     * and the GLFW platform backend. !TODO:Move from here
     */
    void initImGuiVulkan() {
        /* One combined image sampler per texture; only the font atlas so far. */
        const VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 };

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets       = 16;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes    = &poolSize;
        check(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &imguiPool_),
              "vkCreateDescriptorPool (ImGui)");

        const VkFormat colorFormat = swapchain_.format();
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount    = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;
        /**
         * ImGui does not depth test, but its pipeline must declare the same
         * attachments as the VkRenderingInfo it is recorded into.
         */
        renderingInfo.depthAttachmentFormat   = DEPTH_FORMAT;

        ImGui_ImplVulkan_InitInfo info{};
        info.Instance        = ctx_.instance();
        info.PhysicalDevice  = ctx_.physicalDevice();
        info.Device          = device_;
        info.QueueFamily     = ctx_.graphicsQueueFamily();
        info.Queue           = ctx_.graphicsQueue();
        info.DescriptorPool  = imguiPool_;
        info.MinImageCount   = swapchain_.minImageCount();
        info.ImageCount      = swapchain_.imageCount();
        info.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;
        info.UseDynamicRendering       = true;
        info.PipelineRenderingCreateInfo = renderingInfo;
        info.CheckVkResultFn = [](VkResult r) { check(r, "ImGui Vulkan backend"); };

        if (!ImGui_ImplVulkan_Init(&info))
            throw std::runtime_error("ImGui_ImplVulkan_Init failed");
        imguiVulkanInitialized_ = true;
    }

    void drawUI() {
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

    void loadModel() {
        if (modelPath_.empty()) {
            logMessage("no model given; pass a .gltf/.glb path as the first argument");
            return;
        }
        if (!loadGltf(modelPath_, buffers_, uploads_, scene_))
            throw std::runtime_error("failed to load " + modelPath_.string());
        frameScene();
    }

    /// Fits the camera to the loaded scene.
    void frameScene() {
        glm::vec3 min{ std::numeric_limits<float>::max() };
        glm::vec3 max{ std::numeric_limits<float>::lowest() };

        for (const MultiMesh& object : scene_) {
            for (const MultiMeshPart& part : object.parts()) {
                const MeshBounds& bounds = part.mesh->bounds();
                const glm::vec3 center =
                    glm::vec3(part.localTransform * glm::vec4(bounds.center, 1.0f));
                /* Largest axis scale; only used to place a camera. */
                const float scale = std::max({
                    glm::length(glm::vec3(part.localTransform[0])),
                    glm::length(glm::vec3(part.localTransform[1])),
                    glm::length(glm::vec3(part.localTransform[2])) });
                const float radius = bounds.radius * scale;

                min = glm::min(min, center - radius);
                max = glm::max(max, center + radius);
            }
        }

        if (min.x > max.x) { sceneCenter_ = glm::vec3{0.0f}; sceneRadius_ = 1.0f; return; }

        sceneCenter_ = (min + max) * 0.5f;
        sceneRadius_ = std::max(glm::length(max - min) * 0.5f, 1e-3f);
    }

    [[nodiscard]] glm::mat4 viewProjection(VkExtent2D extent) const {
        const float aspect = static_cast<float>(extent.width) /
                             static_cast<float>(std::max(extent.height, 1u));

        const float angle    = static_cast<float>(window_.getElapsedMs()) * 0.0004f;
        const float distance = sceneRadius_ * 2.5f;
        const glm::vec3 eye = sceneCenter_ + distance * glm::vec3{
            std::cos(angle), 0.45f, std::sin(angle) };

        const glm::mat4 view =
            glm::lookAt(eye, sceneCenter_, glm::vec3{0.0f, 1.0f, 0.0f});

        /* Reverse-Z: near and far swapped. */
        glm::mat4 projection = glm::perspective(
            glm::radians(60.0f), aspect, sceneRadius_ * 20.0f, sceneRadius_ * 0.01f);

        /* Vulkan clip space has +Y down. */
        projection[1][1] *= -1.0f;

        return projection * view;
    }


    /**
     * Fills this frame's Objects buffer and dispatches the build pass.
     *
     * The frame slot's fence has signalled by the time drawFrame() records, so
     * last frame's ranges are free to reuse.
     */
    void buildDrawCommands(VkCommandBuffer cmd, VkExtent2D extent) {
        const uint32_t frame = frames_.frameIndex();
        buffers_.resetFrame(frame);
        pendingDraw_ = {};

        std::vector<GPUObject> objects;
        for (const MultiMesh& object : scene_) {
            for (const MultiMeshPart& part : object.parts()) {
                const Mesh& mesh = *part.mesh;
                if (!mesh.uploaded() || mesh.meshletCount() == 0) continue;

                GPUObject record{};
                record.transform = part.localTransform;
                record.meshIndex = mesh.getGpuIndex();
                objects.push_back(record);
            }
        }
        if (objects.empty()) return;

        const auto objectCount = static_cast<uint32_t>(objects.size());

        const BufferSlice objectSlice = buffers_.allocateFrame(
            frame, FrameBufferKind::Objects, objects.size() * sizeof(GPUObject));
        const BufferSlice drawDataSlice = buffers_.allocateFrame(
            frame, FrameBufferKind::DrawData, objects.size() * sizeof(GPUDrawData));
        const BufferSlice commandSlice = buffers_.allocateFrame(
            frame, FrameBufferKind::MeshTaskCommands,
            objects.size() * sizeof(GPUMeshTaskCommand));
        const BufferSlice countSlice = buffers_.allocateFrame(
            frame, FrameBufferKind::MeshTaskCommandCount, sizeof(uint32_t), 4);

        if (!objectSlice || !objectSlice.mapped || !drawDataSlice ||
            !commandSlice || !countSlice) {
            logError("recordFrame: out of per-frame buffer space");
            return;
        }

        std::memcpy(objectSlice.mapped, objects.data(),
                    objects.size() * sizeof(GPUObject));

        vkCmdFillBuffer(cmd, countSlice.buffer, countSlice.offset,
                        sizeof(uint32_t), 0);

        /* The counter is the target of the shader's atomicAdd. */
        VkMemoryBarrier2 clearBarrier{};
        clearBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        clearBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_CLEAR_BIT;
        clearBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        clearBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        clearBarrier.dstAccessMask =
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;

        VkDependencyInfo clearDependency{};
        clearDependency.sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        clearDependency.memoryBarrierCount = 1;
        clearDependency.pMemoryBarriers    = &clearBarrier;
        vkCmdPipelineBarrier2(cmd, &clearDependency);

        const glm::mat4 viewProj = viewProjection(extent);

        BuildDrawCommandsPush push{};
        push.viewProj     = viewProj;
        push.objects      = objectSlice.deviceAddressAs<GPUObject>();
        push.meshes       = buffers_.staticBuffer(StaticBufferKind::Meshes)
                                    .deviceAddressAs<GPUMesh>();
        push.drawData     = drawDataSlice.deviceAddressAs<GPUDrawData>();
        push.commands     = commandSlice.deviceAddressAs<GPUMeshTaskCommand>();
        push.commandCount = countSlice.deviceAddressAs<uint32_t>();
        push.objectCount  = objectCount;

        buildDrawCommands_.record(cmd, push);

        /**
         * The commands and the count are fetched by the indirect draw itself;
         * the mesh shader reads drawData as storage, which is a separate
         * hazard from the command fetch.
         */
        VkMemoryBarrier2 buildBarrier{};
        buildBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        buildBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        buildBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        buildBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
                                     VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT;
        buildBarrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT |
                                     VK_ACCESS_2_SHADER_STORAGE_READ_BIT;

        VkDependencyInfo buildDependency{};
        buildDependency.sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        buildDependency.memoryBarrierCount = 1;
        buildDependency.pMemoryBarriers    = &buildBarrier;
        vkCmdPipelineBarrier2(cmd, &buildDependency);

        pendingDraw_.commands    = commandSlice;
        pendingDraw_.count       = countSlice;
        pendingDraw_.objectCount = objectCount;

        MeshDrawPush& meshPush = pendingDraw_.push;
        meshPush.viewProj = viewProj;
        meshPush.drawData = drawDataSlice.deviceAddressAs<GPUDrawData>();
        meshPush.objects  = objectSlice.deviceAddressAs<GPUObject>();
        meshPush.meshes   = push.meshes;
        meshPush.vertices = buffers_.staticBuffer(StaticBufferKind::Vertices)
                                    .deviceAddressAs<GPUVertex>();
        meshPush.meshlets = buffers_.staticBuffer(StaticBufferKind::Meshlets)
                                    .deviceAddressAs<GPUMeshlet>();
        meshPush.meshletVertexIndices =
            buffers_.staticBuffer(StaticBufferKind::MeshletVertexIndices)
                    .deviceAddressAs<uint32_t>();
        meshPush.meshletTriangles =
            buffers_.staticBuffer(StaticBufferKind::MeshletTriangleIndices)
                    .deviceAddressAs<uint32_t>();
    }

    void recordFrame(VkCommandBuffer cmd, const RenderTarget& target) {
        buildDrawCommands(cmd, target.extent);

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView   = target.view;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = { { 0.2f, 0.1f, 0.3f, 1.0f } };  // same as the GL target

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView   = target.depthView;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
        // Nothing reads depth back yet, so it need not survive the pass.
        depthAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.clearValue.depthStencil.depth = DEPTH_CLEAR;

        VkRenderingInfo rendering{};
        rendering.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea           = VkRect2D{ {0, 0}, target.extent };
        rendering.layerCount           = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments    = &colorAttachment;
        rendering.pDepthAttachment     = &depthAttachment;

        vkCmdBeginRendering(cmd, &rendering);

        meshDraw_.record(cmd, target.extent, pendingDraw_.push,
                         pendingDraw_.commands.buffer, pendingDraw_.commands.offset,
                         pendingDraw_.count.buffer, pendingDraw_.count.offset,
                         pendingDraw_.objectCount);

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

        vkCmdEndRendering(cmd);
    }

    void cleanup() {
        /* Reachable from the destructor after a failed init. */
        if (!device_) return;

        ctx_.waitIdle();

        if (imguiVulkanInitialized_) {
            ImGui_ImplVulkan_Shutdown();
            imguiVulkanInitialized_ = false;
        }
        if (imguiPool_) {
            vkDestroyDescriptorPool(device_, imguiPool_, nullptr);
            imguiPool_ = VK_NULL_HANDLE;
        }

        /* Meshes retire buffer ranges on destruction; must precede shutdown. */
        scene_.clear();
        meshDraw_.destroy();
        buildDrawCommands_.destroy();
        uploads_.destroy();
        buffers_.shutdown();

        frames_.destroy();

        swapchain_.destroy();
        ctx_.shutdown();
    }
};

} // namespace


int main(int argc, char** argv) {
    try {
        /* argv: <model> [frame budget] */
        const std::filesystem::path defaultModelPath =
            std::filesystem::path{__FILE__}.parent_path() / "Assets" / "scene.glb";
        VulkanApp app{argc > 1 ? std::filesystem::path{argv[1]} : defaultModelPath,
                      argc > 2 ? static_cast<uint32_t>(std::atoi(argv[2])) : 0u};
        app.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
