#include <vulkan/vulkan.h>

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

import VkWindow;
import VulkanContext;
import VkSwapchain;
import FrameRunner;
import VkUtil;
import Pipeline;
import ShadersLoader;
import VK_Buffers;
import UploadBatch;
import GltfLoader;
import Logger;

namespace {

constexpr int  WIN_WIDTH  = 1280;
constexpr int  WIN_HEIGHT = 720;
constexpr char WIN_TITLE[] = "PGR_VK";

void check(VkResult r, const char* what) {
    if (r != VK_SUCCESS)
        throw std::runtime_error(std::string(what) + " failed: VkResult " + std::to_string(r));
}

constexpr char SHADER_DIR[] = "Shaders/src";

/**
 * Must match the push_constant block in shaders/gpu_types.glsl field for field.
 * Nothing checks it at build time.
 */
struct DrawPush {
    glm::mat4       viewProj;               // offset 0, model already folded in
    VkDeviceAddress vertices             = 0;   // 64
    VkDeviceAddress meshlets             = 0;   // 72
    VkDeviceAddress meshletVertexIndices = 0;   // 80
    VkDeviceAddress meshletTriangles     = 0;   // 88
    VkDeviceAddress meshes               = 0;   // 96
    VkDeviceAddress materials            = 0;   // 104
    uint32_t        meshIndex            = 0;   // 112
};

/* Guaranteed minimum maxPushConstantsSize. */
static_assert(sizeof(DrawPush) <= 128);

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

        if (!ctx_.init(window_, WIN_TITLE))
            throw std::runtime_error("VulkanContext::init failed");
        device_ = ctx_.device();

        if (!swapchain_.init(ctx_, window_))
            throw std::runtime_error("Swapchain::init failed");

        if (!frames_.init(ctx_, swapchain_))
            throw std::runtime_error("FrameRunner::init failed");

        drawMeshTasks_ = ctx_.cmdDrawMeshTasks();

        shaders_.init(device_);
        shaders_.addIncludeDir(SHADER_DIR);

        if (!buffers_.init(ctx_))
            throw std::runtime_error("VK_buffers::init failed");
        if (!uploads_.init(ctx_, buffers_))
            throw std::runtime_error("UploadBatch::init failed");

        loadModel();

        createPipeline();
        initImGuiVulkan();

        //Swapchains need recreation only on format change
        frames_.setSwapchainRecreatedCallback([this](VkExtent2D, VkFormat format) {
            if (format != pipelineFormat_) {
                vkDestroyPipeline(device_, pipeline_, nullptr);
                vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
                pipeline_       = VK_NULL_HANDLE;
                pipelineLayout_ = VK_NULL_HANDLE;
                createPipeline();
            }
        });

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

    std::filesystem::path  modelPath_;
    uint32_t               maxFrames_   = 0;
    uint32_t               framesDrawn_ = 0;
    std::vector<MultiMesh> scene_;
    glm::vec3              sceneCenter_{0.0f};
    float                  sceneRadius_ = 1.0f;

    PFN_vkCmdDrawMeshTasksEXT drawMeshTasks_ = nullptr;

    VkDevice device_ = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       pipeline_       = VK_NULL_HANDLE;
    /// The format pipeline_ was built against; a swapchain rebuild compares it.
    VkFormat         pipelineFormat_ = VK_FORMAT_UNDEFINED;

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

    VkShaderModule loadShader(const std::string& name) {
        VkShaderModule module = shaders_.load(std::string(SHADER_DIR) + "/" + name);
        if (!module)
            throw std::runtime_error("shader failed to compile: " + name);
        return module;
    }

    void createPipeline() {
        VkShaderModule mesh = loadShader("mesh.mesh");
        VkShaderModule frag = loadShader("mesh.frag");

        const std::array stages{
            shaderStage(VK_SHADER_STAGE_MESH_BIT_EXT, mesh),
            shaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, frag),
        };

        /* Both stages read the block, so both must be named. */
        const VkPushConstantRange pushRange{
            VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(DrawPush),
        };

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges    = &pushRange;
        /* No descriptor sets: buffers are reached by device address. */
        check(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_),
              "vkCreatePipelineLayout");

        pipelineFormat_ = swapchain_.format();

        pipeline_ = createGraphicsPipeline(device_, GraphicsPipelineDesc{
            .stages      = stages,
            .layout      = pipelineLayout_,
            .colorFormat = pipelineFormat_,
        });

        // The pipeline holds everything it needs from them now.
        shaders_.destroy(frag);
        shaders_.destroy(mesh);

        if (!pipeline_)
            throw std::runtime_error("createGraphicsPipeline failed");
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


    void recordFrame(VkCommandBuffer cmd, const RenderTarget& target) {
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

        VkViewport viewport{};
        viewport.x        = 0.0f;
        viewport.y        = 0.0f;
        viewport.width    = static_cast<float>(target.extent.width);
        viewport.height   = static_cast<float>(target.extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        const VkRect2D scissor{ {0, 0}, target.extent };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        drawScene(cmd, target.extent);

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

        vkCmdEndRendering(cmd);
    }

    /**
     * One dispatch per part, one mesh workgroup per meshlet. Direct, not
     * indirect: the GPU-driven path replaces only this function.
     */
    void drawScene(VkCommandBuffer cmd, VkExtent2D extent) {
        if (scene_.empty()) return;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

        DrawPush push{};
        push.vertices  = buffers_.staticBuffer(StaticBufferKind::Vertices).deviceAddress;
        push.meshlets  = buffers_.staticBuffer(StaticBufferKind::Meshlets).deviceAddress;
        push.meshletVertexIndices =
            buffers_.staticBuffer(StaticBufferKind::MeshletVertexIndices).deviceAddress;
        push.meshletTriangles =
            buffers_.staticBuffer(StaticBufferKind::MeshletTriangleIndices).deviceAddress;
        push.meshes    = buffers_.staticBuffer(StaticBufferKind::Meshes).deviceAddress;
        push.materials = buffers_.staticBuffer(StaticBufferKind::Materials).deviceAddress;

        const glm::mat4 viewProj = viewProjection(extent);

        for (const MultiMesh& object : scene_) {
            for (const MultiMeshPart& part : object.parts()) {
                const Mesh& mesh = *part.mesh;
                if (!mesh.uploaded() || mesh.meshletCount() == 0) continue;

                /* Folded: a second mat4 would not fit in 128 bytes. */
                push.viewProj  = viewProj * part.localTransform;
                push.meshIndex = mesh.getGpuIndex();

                vkCmdPushConstants(cmd, pipelineLayout_,
                                   VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(push), &push);

                drawMeshTasks_(cmd, mesh.meshletCount(), 1, 1);
            }
        }
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
        uploads_.destroy();
        buffers_.shutdown();

        frames_.destroy();

        vkDestroyPipeline(device_, pipeline_, nullptr);
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);

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
