module;
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <string>

export module VulkanContext;

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
    void shutdown();

    VkInstance         instance()            const { return vkbInstance_.instance; }
    VkPhysicalDevice   physicalDevice()      const { return vkbDevice_.physical_device; }
    VkDevice           device()              const { return vkbDevice_.device; }
    VkSurfaceKHR       surface()             const { return surface_; }
    VkQueue            graphicsQueue()       const { return graphicsQueue_; }
    uint32_t           graphicsQueueFamily() const { return graphicsQueueFamily_; }
    VmaAllocator       allocator()           const { return allocator_; }
    const std::string& gpuName()             const { return gpuName_; }

    /// vkb::SwapchainBuilder takes a vkb::Device.
    const vkb::Device& vkbDevice() const { return vkbDevice_; }

    PFN_vkCmdDrawMeshTasksEXT cmdDrawMeshTasks() const { return cmdDrawMeshTasks_; }

    void waitIdle() const { vkDeviceWaitIdle(vkbDevice_.device); }

private:
    vkb::Instance vkbInstance_{};
    vkb::Device   vkbDevice_{};
    VkSurfaceKHR  surface_ = VK_NULL_HANDLE;

    VkQueue     graphicsQueue_       = VK_NULL_HANDLE;
    uint32_t    graphicsQueueFamily_ = 0;
    VmaAllocator allocator_          = nullptr;

    std::string gpuName_;

    PFN_vkCmdDrawMeshTasksEXT cmdDrawMeshTasks_ = nullptr;

    bool createInstance(const char* appName);
    bool selectAndCreateDevice();
    bool createAllocator();
    bool loadDeviceExtensionFunctions();
};
