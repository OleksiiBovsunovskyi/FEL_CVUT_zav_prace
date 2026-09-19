module;
#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <filesystem>
#include <vector>

#include <glm/glm.hpp>

export module VulkanApp;

/**
 * Owns the window, Vulkan application state, scene, camera, and application-level frame input.
 * Renderer owns mesh-draw preparation and concrete rendering.
 */

import VkWindow;
import VulkanContext;
import VkSwapchain;
import FrameRunner;
import Renderer;
import VkUtil;
import renderTarget;
import ShadersLoader;
import BufferManager;
import BlockingTransferBatch;
import GltfLoader;
import RenderComponent;
import VkScene;
import GPUTypes;
import ShaderPrint;

/**
 * The renderer: owns the window, the Vulkan objects, the scene and the two
 * passes, and drives them from AppWindow's draw callback.
 */
export class VulkanApp {
public:
    /**
     * @param maxFrames close after this many frames, or 0 to run until closed.
     *        A frame budget lets a script wait for a clean exit, which flushes
     *        stdout; killing the process loses it.
     */
    explicit VulkanApp(uint32_t maxFrames);

    ~VulkanApp();

    VulkanApp(const VulkanApp&)            = delete;
    VulkanApp& operator=(const VulkanApp&) = delete;

    /**
     * Brings up the window, the device and both pipelines. Throws on failure.
     *
     * Nothing is in the scene yet: what to place there is the caller's, and
     * getLoader() is only usable after this returns.
     */
    void init();

    /// Fits the camera to whatever is in the scene, then runs the main loop.
    void run();

    /// The world to fill. Valid for the app's lifetime.
    [[nodiscard]] Scene& getScene() { return scene_; }

    /// Usable only after init(): loading needs the device and the buffers.
    [[nodiscard]] GltfLoader& getLoader() { return loader_; }

private:
    AppWindow     window_;
    VulkanContext ctx_;
    Swapchain     swapchain_;
    FrameRunner   frames_;
    Renderer      renderer_;
    ShaderLoader          shaders_;
    BufferManager         buffers_;
    BlockingTransferBatch transfers_;
    GltfLoader            loader_;
    ShaderPrint           shaderPrint_;

    /// A gap longer than this is a stall, not a frame; the tick gets 0 instead.
    static constexpr int MAX_FRAME_MS = 250;

    uint32_t              maxFrames_   = 0;
    uint32_t              framesDrawn_ = 0;
    int                   lastFrameMs_ = 0;
    Scene                 scene_;
    glm::vec3             sceneCenter_{0.0f};
    float                 sceneRadius_ = 1.0f;

    vk::Device device_ = nullptr;

    vk::DescriptorPool imguiPool_ = nullptr;
    bool               imguiVulkanInitialized_ = false;

    /**
     * Renderer half of the ImGui setup; AppWindow already created the context
     * and the GLFW platform backend. !TODO:Move from here
     */
    void initImGuiVulkan();

    void drawUI();

    /**
     * Measures the scene and gives it a camera if it has none. The bounds are
     * what the orbit is sized from; the camera itself is an ordinary Object.
     */
    void frameScene();

    /**
     * !TODO: make proper player and remove orbit
     */
    void updateCamera();

    /**
     * @return seconds since the previous frame, 0 when there is no previous
     *         one or the gap is too long to integrate against.
     */
    [[nodiscard]] float elapseFrame();

    /**
     * Refills drawList_ from the scene, then flattens it into one GPUMeshInstance per
     * drawable part.
     *
     * @return the instances to draw this frame, empty when there is nothing.
     */
    [[nodiscard]] std::vector<GPUMeshInstance> collectMeshInstances();

    /**
     * Flattens the scene, computes the view-projection matrix, and calls Renderer.
     */
    void recordFrame(Frame::Recording& recording);

    /// Records ImGui inside ForwardRenderer's dynamic-rendering pass.
    void recordImGui(vk::CommandBuffer cmd, vk::Extent2D extent);

    void cleanup();
};
