module;
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>

#include <functional>
#include <string>

export module VulkanContext;

import vulkan;
import vk_mem_alloc;
import VkWindow;

/**
 * Instance, surface, device, queues and the VMA allocator.
 */
export class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext() = default;

    VulkanContext(const VulkanContext&)            = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    /**Builds the instance, creates the window surface, selects a GPU that
    supports the GPU-driven feature set below, and creates the device.**/
    bool init(const AppWindow& window, const char* appName);

    /**
     * Routes validation messages of INFO severity - which is what
     * debugPrintfEXT output arrives as - to `sink`. Warnings and errors keep
     * going to the log. Must be called before init().
     *
     * @param sink receives one whole message per call, from the submitting
     *        thread.
     */
    void setDebugMessageSink(std::function<void(const std::string&)> sink) {
        debugSink_ = std::move(sink);
    }
    void shutdown();

    vk::Instance       instance()            const { return vkbInstance_.instance; }
    /// vkb::PhysicalDevice converts to the handle, so the cast picks that out.
    vk::PhysicalDevice physicalDevice()      const {
        return static_cast<VkPhysicalDevice>(vkbDevice_.physical_device);
    }
    vk::Device         device()              const { return vkbDevice_.device; }
    vk::SurfaceKHR     surface()             const { return surface_; }
    vk::Queue          graphicsQueue()       const { return graphicsQueue_; }
    uint32_t           graphicsQueueFamily() const { return graphicsQueueFamily_; }
    vma::Allocator     allocator()           const { return allocator_; }
    const std::string& gpuName()             const { return gpuName_; }

    /// vkb::SwapchainBuilder takes a vkb::Device.
    const vkb::Device& vkbDevice() const { return vkbDevice_; }

    /// VK_EXT_mesh_shader entry points, resolved during init().
    PFN_vkCmdDrawMeshTasksEXT cmdDrawMeshTasks() const {
        return cmdDrawMeshTasks_;
    }
    PFN_vkCmdDrawMeshTasksIndirectCountEXT cmdDrawMeshTasksIndirectCount() const {
        return cmdDrawMeshTasksIndirectCount_;
    }

    void waitIdle() const { vkDeviceWaitIdle(vkbDevice_.device); }

private:
    vkb::Instance vkbInstance_{};
    vkb::Device   vkbDevice_{};
    VkSurfaceKHR  surface_ = VK_NULL_HANDLE;

    VkQueue        graphicsQueue_       = VK_NULL_HANDLE;
    uint32_t       graphicsQueueFamily_ = 0;
    vma::Allocator allocator_           = nullptr;

    std::string gpuName_;

    PFN_vkCmdDrawMeshTasksEXT              cmdDrawMeshTasks_             = nullptr;
    PFN_vkCmdDrawMeshTasksIndirectCountEXT cmdDrawMeshTasksIndirectCount_ = nullptr;

    std::function<void(const std::string&)> debugSink_;

    bool createInstance(const char* appName);
    bool selectAndCreateDevice();
    bool createAllocator();
    bool loadDeviceExtensionFunctions();
};
