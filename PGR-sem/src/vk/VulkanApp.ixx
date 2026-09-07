module;
#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <filesystem>
#include <vector>

#include <glm/glm.hpp>

export module VulkanApp;

/**
 * Owns every renderer-wide object and the order they run in. The modules it
 * drives each own one piece and know nothing about the frame as a whole:
 * AppWindow the window and the ImGui frame, VulkanContext the device and the
 * allocator, Swapchain the presentable images, FrameRunner the pacing,
 * BufferManager the memory, BuildDrawCommands and MeshDraw one pipeline each.
 *
 * What lives here and nowhere else: the scene, the camera, the per-frame
 * GPUMeshInstance array, the push constant contents and the barriers between the two
 * passes.
 *
 * Startup, in order - each step needs the one above it:
 *
 *     AppWindow -> VulkanContext -> Swapchain -> FrameRunner -> ShaderLoader
 *     -> BufferManager -> UploadBatch -> GltfLoader -> the two pipelines -> ImGui
 *
 * Loading needs only BufferManager and UploadBatch; its place before the pipelines
 * is not a dependency. It is the only caller of allocateStatic, and it blocks
 * on the GPU once per primitive.
 *
 * Then AppWindow::mainLoop drives one callback per frame, which calls
 * FrameRunner::drawFrame, which calls back into recordFrame with a begun
 * command buffer and a target already in COLOR_ATTACHMENT_OPTIMAL:
 *
 *  1. collectMeshInstances flattens the scene into one GPUMeshInstance per drawable part.
 *     Everything after this is per-frame; geometry is already on the GPU.
 *  2. allocateFrameSpans bump-allocates this frame's four ranges. Legal
 *     because buildDrawCommands called resetFrame first, and drawFrame had
 *     already waited on that slot's fence.
 *  3. The instances are memcpy'd into the mapped MeshInstances span - the only
 *     CPU->GPU traffic left after load.
 *  4. recordBuildDrawCommands zeroes the draw counter, dispatches one compute
 *     invocation per instance, and barriers the result into DRAW_INDIRECT and
 *     MESH_SHADER. The shader frustum-tests each instance and claims a slot with
 *     an atomic; the count stays on the GPU.
 *  5. makeMeshDrawPush gathers the static mega-buffer addresses the mesh shader
 *     walks, and the whole draw is published as pendingDraw_.
 *  6. recordFrame opens the render pass, issues the one indirect draw, lets
 *     ImGui record into the same pass, and closes it.
 *
 * FrameRunner submits and presents; nothing here waits on the GPU after load.
 */

import VkWindow;
import VulkanContext;
import VkSwapchain;
import FrameRunner;
import VkUtil;
import ShadersLoader;
import BufferManager;
import UploadBatch;
import GltfLoader;
import RenderComponent;
import VkScene;
import BuildDrawCommands;
import MeshDraw;
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
    ShaderLoader  shaders_;
    BufferManager buffers_;
    UploadBatch   uploads_;
    GltfLoader    loader_;
    BuildDrawCommands buildDrawCommands_;
    MeshDraw          meshDraw_;
    ShaderPrint       shaderPrint_;

    /** 
     * What buildDrawCommands() produced this frame, consumed by the indirect
     * draw in recordFrame(). instanceCount 0 means there is nothing to draw.
     */
    struct PendingDraw {
        BufferRegion commands{};
        BufferRegion count{};
        GPUMeshDrawPush push{};
        uint32_t     instanceCount = 0;
    };
    PendingDraw pendingDraw_;

    /// The four per-frame ranges one draw needs, allocated together.
    /* Which span each range is declared as is the contract: only `instances` is
     * CPU-written, and the other three are GPU output. */
    struct FrameSpans {
        MappedSpan<GPUMeshInstance>    instances{};
        DeviceSpan<GPUDrawData>        drawData{};
        DeviceSpan<GPUMeshTaskCommand> commands{};
        DeviceSpan<uint32_t>           count{};

        /// @return true when all four were allocated.
        [[nodiscard]] explicit operator bool() const {
            return instances && drawData && commands && count;
        }
    };

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
     * @param frameIndex frame slot to allocate from.
     * @param instanceCount instances the ranges must hold.
     * @return the four ranges, or one that tests false when any failed.
     */
    [[nodiscard]] FrameSpans allocateFrameSpans(uint32_t frameIndex,
                                                uint32_t instanceCount);

    /**
     * Zeroes the draw counter, records the BuildDrawCommands pass and barriers
     * its output into the indirect draw and the mesh shader.
     *
     * @param instanceCount invocations to dispatch, one per instance.
     */
    void recordBuildDrawCommands(vk::CommandBuffer cmd, const FrameSpans& spans,
                                 const glm::mat4& viewProj,
                                 uint32_t instanceCount) const;

    /// @return the mesh pass push constants: this frame's two arrays plus the
    ///         Materials base the fragment shader indexes.
    [[nodiscard]] GPUMeshDrawPush makeMeshDrawPush(const FrameSpans& spans,
                                                   const glm::mat4& viewProj) const;


    /**
     * Assembles this frame's draw into pendingDraw_, ready for recordFrame().
     *
     * The frame slot's fence has signalled by the time drawFrame() records, so
     * last frame's ranges are free to reuse.
     */
    void buildDrawCommands(vk::CommandBuffer cmd, vk::Extent2D extent);

    void recordFrame(vk::CommandBuffer cmd, const RenderTarget_Old& target);

    void cleanup();
};
