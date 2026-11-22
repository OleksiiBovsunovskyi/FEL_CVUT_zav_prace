module;

#include <cstdint>
#include <filesystem>
#include <span>

export module VulkanApp;

import vulkan;
import VulkanContext;
import PresentationBridge;
import ExternalTarget;
import FrameRunner;
import Renderer;
import VkUtil;
import ShadersLoader;
import BufferManager;
import BlockingTransferBatch;
import ModelLoader;
import RenderComponent;
import VkScene;
import InputHandler;
import GPUTypes;
import TextureManager;
import Releaser;

/**
 * Owns the Vulkan objects, the scene, the renderer, and the render loop.
 * Frames are drawn into ExternalTarget and presented through PresentationBridge.
 */
export class VulkanApp {
public:
    VulkanApp() = default;
    ~VulkanApp();

    VulkanApp(const VulkanApp&)            = delete;
    VulkanApp& operator=(const VulkanApp&) = delete;

    /**
     * Brings up the device, the external present target and the pipelines. Throws on failure.
     * @param bridge host bridge providing input, resize, and GPU presentation callbacks.
     * @param extent initial frame size in texels.
     * @param dataRoot folder holding the Shaders folder.
     * @param preferredDeviceUuid 16-byte GPU UUID to match when non-empty.
     * @note Nothing is in the scene yet; getLoader() is usable after this returns.
     */
    void init(PresentationBridge& bridge, vk::Extent2D extent, const std::filesystem::path& dataRoot,
              std::span<const uint8_t> preferredDeviceUuid = {});

    /// Runs the render loop until the bridge requests stop or the device is lost.
    void run();

    /// The world to fill. Valid for the app's lifetime.
    [[nodiscard]] Scene& getScene() { return scene_; }

    /// Usable only after init(): loading needs the device and the buffers.
    [[nodiscard]] ModelLoader& getLoader() { return loader_; }

private:
    PresentationBridge* bridge_ = nullptr;
    InputHandler  input_;
    VulkanContext ctx_;
    ExternalTarget target_;
    FrameRunner   frames_;
    Renderer      renderer_;
    Releaser              releaser_;
    TextureManager        textures_;
    ShaderLoader          shaders_;
    BufferManager         buffers_;
    BlockingTransferBatch transfers_;
    ModelLoader           loader_;

    /// A gap longer than this is a stall, not a frame; the tick gets 0 instead.
    static constexpr int MAX_FRAME_MS = 250;

    int                   lastFrameMs_ = 0;
    Scene                 scene_;

    vk::Device device_ = nullptr;

    /// Broadcasts everything reported since the previous frame.
    void dispatchInput();

    /**
     * @return seconds since the previous frame, 0 when there is no previous
     *         one or the gap is too long to integrate against.
     */
    [[nodiscard]] float elapseFrame();

    /**
     * Hands the scene's instances and what changed in them to Renderer, with
     * the view-projection matrix.
     */
    void recordFrame(Frame::Recording& recording);

    void cleanup();
};
