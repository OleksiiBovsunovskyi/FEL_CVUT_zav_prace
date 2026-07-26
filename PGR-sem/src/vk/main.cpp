/**
 * vertex-buffer-less triangle, drawn with dynamic rendering.
 */

#include <VkBootstrap.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vk_mem_alloc.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int  WIN_WIDTH  = 1280;
constexpr int  WIN_HEIGHT = 720;
constexpr char WIN_TITLE[] = "PGR_VK";
constexpr uint32_t FRAMES_IN_FLIGHT = 2;

void check(VkResult r, const char* what) {
    if (r != VK_SUCCESS)
        throw std::runtime_error(std::string(what) + " failed: VkResult " + std::to_string(r));
}

std::vector<char> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::ate | std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("cannot open " + path);
    const auto size = static_cast<size_t>(f.tellg());
    std::vector<char> buffer(size);
    f.seekg(0);
    f.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}


class VulkanApp {
public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    GLFWwindow* window_ = nullptr;

    vkb::Instance       vkbInstance_{};
    vkb::Device         vkbDevice_{};
    vkb::Swapchain      vkbSwapchain_{};
    VkSurfaceKHR        surface_ = VK_NULL_HANDLE;
    VkDevice            device_  = VK_NULL_HANDLE;
    VkQueue             graphicsQueue_ = VK_NULL_HANDLE;
    uint32_t            graphicsQueueFamily_ = 0;
    VmaAllocator        allocator_ = nullptr;

    std::vector<VkImage>     swapImages_;
    std::vector<VkImageView> swapImageViews_;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       pipeline_       = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, FRAMES_IN_FLIGHT> commandBuffers_{};
    std::array<VkFence,         FRAMES_IN_FLIGHT> inFlightFences_{};
    std::array<VkSemaphore,     FRAMES_IN_FLIGHT> imageAvailable_{};

    std::vector<VkSemaphore> renderFinished_;

    uint32_t currentFrame_ = 0;
    bool     framebufferResized_ = false;

    void initWindow() {
        if (!glfwInit())
            throw std::runtime_error("glfwInit failed");

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);   // no GL context wanted
        window_ = glfwCreateWindow(WIN_WIDTH, WIN_HEIGHT, WIN_TITLE, nullptr, nullptr);
        if (!window_)
            throw std::runtime_error("glfwCreateWindow failed");

        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* w, int, int) {
            static_cast<VulkanApp*>(glfwGetWindowUserPointer(w))->framebufferResized_ = true;
        });
        glfwSetKeyCallback(window_, [](GLFWwindow* w, int key, int, int action, int) {
            if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
                glfwSetWindowShouldClose(w, GLFW_TRUE);
        });
    }


    void initVulkan() {
        createInstanceAndDevice();
        createAllocator();
        createSwapchain();
        createCommandObjects();
        createSyncObjects();
        createPipeline();
    }

    void createInstanceAndDevice() {
        vkb::InstanceBuilder builder;
        builder.set_app_name("PGR_VK")
               .require_api_version(1, 3, 0);
#ifndef NDEBUG
        builder.request_validation_layers(true)
               .use_default_debug_messenger();
#endif
        auto instRet = builder.build();
        if (!instRet)
            throw std::runtime_error("vkb instance: " + instRet.error().message());
        vkbInstance_ = instRet.value();

        check(glfwCreateWindowSurface(vkbInstance_.instance, window_, nullptr, &surface_),
              "glfwCreateWindowSurface");


        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.dynamicRendering = VK_TRUE;
        features13.synchronization2 = VK_TRUE;


        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.bufferDeviceAddress                    = VK_TRUE;
        features12.descriptorIndexing                     = VK_TRUE;
        features12.runtimeDescriptorArray                 = VK_TRUE;
        features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        features12.drawIndirectCount                      = VK_TRUE;

        vkb::PhysicalDeviceSelector selector{vkbInstance_};
        auto physRet = selector.set_surface(surface_)
                               .set_minimum_version(1, 3)
                               .set_required_features_13(features13)
                               .set_required_features_12(features12)
                               .select();
        if (!physRet)
            throw std::runtime_error("vkb physical device: " + physRet.error().message());

        std::printf("GPU: %s\n", physRet.value().properties.deviceName);

        auto devRet = vkb::DeviceBuilder{physRet.value()}.build();
        if (!devRet)
            throw std::runtime_error("vkb device: " + devRet.error().message());

        vkbDevice_ = devRet.value();
        device_    = vkbDevice_.device;

        auto queueRet = vkbDevice_.get_queue(vkb::QueueType::graphics);
        if (!queueRet)
            throw std::runtime_error("no graphics queue: " + queueRet.error().message());
        graphicsQueue_       = queueRet.value();
        graphicsQueueFamily_ = vkbDevice_.get_queue_index(vkb::QueueType::graphics).value();
    }

    void createAllocator() {
        VmaAllocatorCreateInfo info{};
        info.physicalDevice = vkbDevice_.physical_device;
        info.device         = device_;
        info.instance       = vkbInstance_.instance;
        info.vulkanApiVersion = VK_API_VERSION_1_3;

        info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        check(vmaCreateAllocator(&info, &allocator_), "vmaCreateAllocator");
    }

    void createSwapchain() {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);

        vkb::SwapchainBuilder builder{vkbDevice_};
        auto ret = builder
            .set_desired_format(VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM,
                                                   VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
            .set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
            .set_desired_extent(static_cast<uint32_t>(w), static_cast<uint32_t>(h))
            .add_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
            .set_old_swapchain(vkbSwapchain_)
            .build();
        if (!ret)
            throw std::runtime_error("vkb swapchain: " + ret.error().message());

        vkb::destroy_swapchain(vkbSwapchain_);
        vkbSwapchain_   = ret.value();
        swapImages_     = vkbSwapchain_.get_images().value();
        swapImageViews_ = vkbSwapchain_.get_image_views().value();
    }

    void destroySwapchain() {
        vkbSwapchain_.destroy_image_views(swapImageViews_);
        swapImageViews_.clear();
        swapImages_.clear();
    }

    void recreateSwapchain() {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        while (w == 0 || h == 0) {
            glfwGetFramebufferSize(window_, &w, &h);
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(device_);
        destroySwapchain();
        createSwapchain();
        recreateRenderFinishedSemaphores();
    }

    void createCommandObjects() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = graphicsQueueFamily_;
        check(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_), "vkCreateCommandPool");

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = commandPool_;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = FRAMES_IN_FLIGHT;
        check(vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()),
              "vkAllocateCommandBuffers");
    }

    void createSyncObjects() {
        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;   // frame 0 must not block

        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
            check(vkCreateSemaphore(device_, &semInfo, nullptr, &imageAvailable_[i]), "vkCreateSemaphore");
            check(vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]), "vkCreateFence");
        }
        recreateRenderFinishedSemaphores();
    }

    void recreateRenderFinishedSemaphores() {
        for (VkSemaphore s : renderFinished_)
            vkDestroySemaphore(device_, s, nullptr);
        renderFinished_.assign(swapImages_.size(), VK_NULL_HANDLE);

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (auto& s : renderFinished_)
            check(vkCreateSemaphore(device_, &semInfo, nullptr, &s), "vkCreateSemaphore");
    }

    VkShaderModule loadShader(const std::string& path) {
        const auto code = readFile(path);
        VkShaderModuleCreateInfo info{};
        info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size();
        info.pCode    = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule module = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device_, &info, nullptr, &module), "vkCreateShaderModule");
        return module;
    }

    void createPipeline() {
        VkShaderModule vert = loadShader("Shaders/triangle.vert.spv");
        VkShaderModule frag = loadShader("Shaders/triangle.frag.spv");

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName  = "main";

        VkPipelineVertexInputStateCreateInfo vertexInput{};   // nothing bound yet
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount  = 1;

        const std::array<VkDynamicState, 2> dynamicStates{
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates    = dynamicStates.data();

        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode    = VK_CULL_MODE_NONE;   // enable once real geometry lands

        raster.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachment.blendEnable    = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments    = &blendAttachment;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        check(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_),
              "vkCreatePipelineLayout");

        const VkFormat colorFormat = vkbSwapchain_.image_format;
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount    = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext               = &renderingInfo;
        pipelineInfo.stageCount          = 2;
        pipelineInfo.pStages             = stages;
        pipelineInfo.pVertexInputState   = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState      = &viewportState;
        pipelineInfo.pRasterizationState = &raster;
        pipelineInfo.pMultisampleState   = &multisample;
        pipelineInfo.pColorBlendState    = &blend;
        pipelineInfo.pDynamicState       = &dynamicState;
        pipelineInfo.layout              = pipelineLayout_;
        pipelineInfo.renderPass          = VK_NULL_HANDLE;   // dynamic rendering

        check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                        nullptr, &pipeline_),
              "vkCreateGraphicsPipelines");

        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }

    /// synchronization2 image layout transition.
    static void transitionImage(VkCommandBuffer cmd, VkImage image,
                                VkImageLayout oldLayout, VkImageLayout newLayout,
                                VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask  = srcStage;
        barrier.srcAccessMask = srcAccess;
        barrier.dstStageMask  = dstStage;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout     = oldLayout;
        barrier.newLayout     = newLayout;
        barrier.image         = image;
        barrier.subresourceRange = VkImageSubresourceRange{
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        VkDependencyInfo dep{};
        dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers    = &barrier;

        vkCmdPipelineBarrier2(cmd, &dep);
    }

    void recordCommands(VkCommandBuffer cmd, uint32_t imageIndex) {
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        check(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer");

        transitionImage(cmd, swapImages_[imageIndex],
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView   = swapImageViews_[imageIndex];
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = { { 0.2f, 0.1f, 0.3f, 1.0f } };  // same as the GL target

        const VkExtent2D extent = vkbSwapchain_.extent;

        VkRenderingInfo rendering{};
        rendering.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea           = VkRect2D{ {0, 0}, extent };
        rendering.layerCount           = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments    = &colorAttachment;

        vkCmdBeginRendering(cmd, &rendering);

        VkViewport viewport{};
        viewport.x        = 0.0f;
        viewport.y        = 0.0f;
        viewport.width    = static_cast<float>(extent.width);
        viewport.height   = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        const VkRect2D scissor{ {0, 0}, extent };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRendering(cmd);

        transitionImage(cmd, swapImages_[imageIndex],
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);

        check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");
    }

    void drawFrame() {
        VkFence fence = inFlightFences_[currentFrame_];
        check(vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");

        uint32_t imageIndex = 0;
        VkResult acquire = vkAcquireNextImageKHR(device_, vkbSwapchain_.swapchain, UINT64_MAX,
                                                 imageAvailable_[currentFrame_],
                                                 VK_NULL_HANDLE, &imageIndex);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain();
            return;
        }
        if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
            check(acquire, "vkAcquireNextImageKHR");

        check(vkResetFences(device_, 1, &fence), "vkResetFences");

        VkCommandBuffer cmd = commandBuffers_[currentFrame_];
        check(vkResetCommandBuffer(cmd, 0), "vkResetCommandBuffer");
        recordCommands(cmd, imageIndex);

        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{};
        submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount   = 1;
        submit.pWaitSemaphores      = &imageAvailable_[currentFrame_];
        submit.pWaitDstStageMask    = &waitStage;
        submit.commandBufferCount   = 1;
        submit.pCommandBuffers      = &cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores    = &renderFinished_[imageIndex];
        check(vkQueueSubmit(graphicsQueue_, 1, &submit, fence), "vkQueueSubmit");

        VkPresentInfoKHR present{};
        present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores    = &renderFinished_[imageIndex];
        present.swapchainCount     = 1;
        present.pSwapchains        = &vkbSwapchain_.swapchain;
        present.pImageIndices      = &imageIndex;

        VkResult presentRes = vkQueuePresentKHR(graphicsQueue_, &present);
        if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR ||
            framebufferResized_) {
            framebufferResized_ = false;
            recreateSwapchain();
        } else {
            check(presentRes, "vkQueuePresentKHR");
        }

        currentFrame_ = (currentFrame_ + 1) % FRAMES_IN_FLIGHT;
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(window_)) {
            glfwPollEvents();
            drawFrame();
        }
        vkDeviceWaitIdle(device_);
    }

    void cleanup() {
        for (VkSemaphore s : renderFinished_)
            vkDestroySemaphore(device_, s, nullptr);
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
            vkDestroySemaphore(device_, imageAvailable_[i], nullptr);
            vkDestroyFence(device_, inFlightFences_[i], nullptr);
        }
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        vkDestroyPipeline(device_, pipeline_, nullptr);
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);

        destroySwapchain();
        vkb::destroy_swapchain(vkbSwapchain_);

        if (allocator_) vmaDestroyAllocator(allocator_);

        vkb::destroy_device(vkbDevice_);
        vkb::destroy_surface(vkbInstance_, surface_);
        vkb::destroy_instance(vkbInstance_);

        if (window_) glfwDestroyWindow(window_);
        glfwTerminate();
    }
};

} // namespace


int main() {
    try {
        VulkanApp app;
        app.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
