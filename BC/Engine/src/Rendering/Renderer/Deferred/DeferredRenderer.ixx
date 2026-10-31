module;


export module DeferredRenderer;
import VulkanContext;
import vulkan;
import VulkanContext;
import Frame;
import GPUTypes;
import GpuPassTimings;
import DrawPass;
import MeshDrawResources;
import ShadersLoader;
import TextureManager;
import Image;
import Logger;
import GPUVisibilityPass;
import VisibilityByfferImageDesc;
import RendererConfig;

export class DeferredRenderer
{
public:    
    /**
     * Creates the mesh-draw pipeline.
     * @param ctx Vulkan context supplying device and indirect-draw function.
     * @param shaderLoader loads the mesh and fragment shader modules.
     * @param meshShaderPath mesh shader SPIR-V file.
     * @param fragmentShaderPath fragment shader SPIR-V file.
     * @param visibilityFragmentShaderPath fragment shader SPIR-V file for the visibility pass.
     * @param colorFormat swapchain format.
     * @param extent rendering size in texels.
     * @param textureLayout TextureManager's bindless set layout, bound as set 0.
     * @return false on pipeline creation failure.
     */
    [[nodiscard]] bool init(VulkanContext& ctx, ShaderLoader& shaderLoader,
                            const std::filesystem::path& meshShaderPath,
                            const std::filesystem::path& fragmentShaderPath,
                            const std::filesystem::path& visibilityFragmentShaderPath,
                            vk::Format colorFormat, vk::Extent2D extent,
                            vk::DescriptorSetLayout textureLayout)
    {
      if (!visibilityPass_.init(ctx.device(), shaderLoader, meshShaderPath, visibilityFragmentShaderPath,
                            visibilityBufferDesc(extent).format, DEPTH_FORMAT, ctx.cmdDrawMeshTasksIndirectCount(),
                            textureLayout))
      {
          logError("Failed to initialize visibility pass");
        return false;
      }
        if (!drawPass_.init(ctx.device(), shaderLoader, meshShaderPath, fragmentShaderPath,
                            colorFormat, DEPTH_FORMAT, ctx.cmdDrawMeshTasksIndirectCount(),
                            textureLayout))
        {
            logError("Failed to initialize draw pass");
            return false;
        }
      return true;
    }
       /**
    * Records dynamic rendering, the prepared mesh draw, and the draw callback.
    * @param recording active Frame recording interface.
    * @param preparedMeshDraw prepared mesh-draw resources for this recording.
    * @param frameData address of this recording's GPUFrameData.
    * @param materials base address of the Materials mega-buffer.
    * @param textureSet TextureManager's bindless set.
    * @param depthAttachment depth attachment prepared for this recording.
    * @param visibilityAttachment visibility attachment prepared for this recording.
    * @param drawCallback callback recorded inside the rendering pass.
    * @param timings marked after the mesh draw and after the callback.
    * @param depthPyramidSlot sampled slot of the full depth pyramid chain.
    */
    void render(Frame::Recording& recording, const PreparedMeshDraw& preparedMeshDraw,
                GpuPtr<GPUFrameData> frameData,
                GpuPtr<GPUMaterial> materials, vk::DescriptorSet textureSet,
                const vk::RenderingAttachmentInfo& depthAttachment,
                const vk::RenderingAttachmentInfo& visibilityAttachment,
                const std::function<void(vk::CommandBuffer, vk::Extent2D)>& drawCallback,
                GpuPassTimings& timings, TextureSlot depthPyramidSlot)
    {
        const vk::CommandBuffer cmd = recording.commandBuffer();
    
        if (preparedMeshDraw)
        {
            //!Visibility pass
            vk::RenderingInfo visibilityRenderingInfo{};
            visibilityRenderingInfo.renderArea = vk::Rect2D{{0, 0}, recording.extent()};
            visibilityRenderingInfo.layerCount = 1;
            visibilityRenderingInfo.colorAttachmentCount = 1;
            visibilityRenderingInfo.pColorAttachments = &visibilityAttachment;
            visibilityRenderingInfo.pDepthAttachment = &depthAttachment;

            cmd.beginRendering(visibilityRenderingInfo);
            GPUVisibilityPassPush visibilityPush{
                .frame = frameData,
                .drawData = preparedMeshDraw.drawData,
                .instances = preparedMeshDraw.instances,
                .depthPyramidSlot = depthPyramidSlot.value,
            };
            visibilityPass_.record(
                cmd, 
                recording.extent(), 
                visibilityPush,
                preparedMeshDraw.indirectCommands,
                preparedMeshDraw.indirectCount,
                preparedMeshDraw.instanceCount, 
                textureSet);
            timings.mark(cmd, "Deferred renderer: Visibility pass");
            cmd.endRendering();
            
            //!Draw pass
            const vk::RenderingAttachmentInfo colorAttachment =
                recording.getColorAttachment(vk::ClearColorValue{std::array<float, 4>{0.2f, 0.1f, 0.3f, 1.0f}});
            //depthAttachment.loadOp = vk::AttachmentLoadOp::eLoad; TODO depth attachment is const, but we should keep it for the draw pass.
            vk::RenderingInfo DrawRenderingInfo{};
            DrawRenderingInfo.renderArea = vk::Rect2D{{0, 0}, recording.extent()};
            DrawRenderingInfo.layerCount = 1;
            DrawRenderingInfo.colorAttachmentCount = 1;
            DrawRenderingInfo.pColorAttachments = &colorAttachment;
            DrawRenderingInfo.pDepthAttachment = &depthAttachment;
            cmd.beginRendering(DrawRenderingInfo);
            GPUMeshDrawPush drawPush{
                .frame = frameData,
                .drawData = preparedMeshDraw.drawData,
                .instances = preparedMeshDraw.instances,
                .materials = materials,
                .depthPyramidSlot = depthPyramidSlot.value
            };
            drawPass_.record(
                cmd,
                recording.extent(),
                drawPush,preparedMeshDraw.indirectCommands,
                preparedMeshDraw.indirectCount,
                preparedMeshDraw.instanceCount,
                textureSet);
            timings.mark(cmd, "Deferred renderer: Draw pass");
            cmd.endRendering();
        }
            
        
        if (drawCallback) {
            drawCallback(cmd, recording.extent());
            timings.mark(cmd, "draw callback");
        }
 
    }

private:
    DrawPass<GPUMeshDrawPush>       drawPass_;
    DrawPass<GPUVisibilityPassPush> visibilityPass_;
};
