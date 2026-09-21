module;

#include <filesystem>
#include <functional>

#include <glm/glm.hpp>

export module ForwardRenderer;

import vulkan;
import VulkanContext;
import Frame;
import GPUTypes;
import GpuPassTimings;
import MeshDraw;
import MeshDrawResources;
import ShadersLoader;

/**
 * Records the forward rendering pass.
 */
export class ForwardRenderer {
public:
    ForwardRenderer() = default;
    ~ForwardRenderer() { destroy(); }

    ForwardRenderer(const ForwardRenderer&)            = delete;
    ForwardRenderer& operator=(const ForwardRenderer&) = delete;

    /**
     * Creates the mesh-draw pipeline.
     * @param ctx Vulkan context supplying device and indirect-draw function.
     * @param shaderLoader loads the mesh and fragment shader modules.
     * @param meshShaderPath mesh shader SPIR-V file.
     * @param fragmentShaderPath fragment shader SPIR-V file.
     * @param colorFormat swapchain format.
     * @param extent rendering size in texels.
     * @param textureLayout TextureManager's bindless set layout, bound as set 0.
     * @return false on pipeline creation failure.
     */
    [[nodiscard]] bool init(VulkanContext& ctx, ShaderLoader& shaderLoader,
                            const std::filesystem::path& meshShaderPath,
                            const std::filesystem::path& fragmentShaderPath,
                            vk::Format colorFormat, vk::Extent2D extent,
                            vk::DescriptorSetLayout textureLayout);

    /// Destroys the mesh-draw pipeline.
    void destroy();

    /**
     * Records dynamic rendering, the prepared mesh draw, and the draw callback.
     * @param recording active Frame recording interface.
     * @param meshDraw prepared mesh-draw resources for this recording.
     * @param cameraData address of this recording's camera record.
     * @param materials base address of the Materials mega-buffer.
     * @param textureSet TextureManager's bindless set.
     * @param depthAttachment depth attachment prepared for this recording.
     * @param drawCallback callback recorded inside the rendering pass.
     * @param timings marked after the mesh draw and after the callback.
     */
    void render(Frame::Recording& recording, const PreparedMeshDraw& meshDraw,
                GpuPtr<GPUCameraData> cameraData,
                GpuPtr<GPUMaterial> materials, vk::DescriptorSet textureSet,
                const vk::RenderingAttachmentInfo& depthAttachment,
                const std::function<void(vk::CommandBuffer, vk::Extent2D)>& drawCallback,
                GpuPassTimings& timings);

    /**
     * Receives the new rendering size.
     * @param extent new size in texels.
     * @return true.
     */
    [[nodiscard]] bool resize(vk::Extent2D extent);

private:
    MeshDraw meshDraw_;
};
