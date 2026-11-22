module;
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#endif
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>

#include <span>
#include <string>

export module VulkanContext;

import vulkan;
import vk_mem_alloc;

/**
 * Instance, device, queues and the VMA allocator, with no surface and no presentation support required.
 */
export class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext() = default;

    VulkanContext(const VulkanContext&)            = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    /**
     * Builds the instance, selects a GPU that supports the required feature set, and creates the device.
     * @param appName Vulkan application name.
     * @param preferredDeviceUuid 16-byte GPU UUID to match when non-empty.
     */
    bool init(const char* appName, std::span<const uint8_t> preferredDeviceUuid = {});

    void shutdown();

    vk::Instance       instance()            const { return vkbInstance_.instance; }
    /// vkb::PhysicalDevice converts to the handle, so the cast picks that out.
    vk::PhysicalDevice physicalDevice()      const {
        return static_cast<VkPhysicalDevice>(vkbDevice_.physical_device);
    }
    vk::Device         device()              const { return vkbDevice_.device; }
    vk::Queue          graphicsQueue()       const { return graphicsQueue_; }
    uint32_t           graphicsQueueFamily() const { return graphicsQueueFamily_; }
    vma::Allocator     allocator()           const { return allocator_; }
    const std::string& gpuName()             const { return gpuName_; }

    /// VK_EXT_mesh_shader entry points, resolved during init().
    PFN_vkCmdDrawMeshTasksEXT cmdDrawMeshTasks() const {
        return cmdDrawMeshTasks_;
    }
    PFN_vkCmdDrawMeshTasksIndirectCountEXT cmdDrawMeshTasksIndirectCount() const {
        return cmdDrawMeshTasksIndirectCount_;
    }

#ifdef _WIN32
    /// VK_KHR_external_memory_win32 / VK_KHR_external_semaphore_win32 entry points, resolved during init().
    PFN_vkGetMemoryWin32HandleKHR getMemoryWin32Handle() const {
        return getMemoryWin32Handle_;
    }
    PFN_vkGetSemaphoreWin32HandleKHR getSemaphoreWin32Handle() const {
        return getSemaphoreWin32Handle_;
    }
#endif

    void waitIdle() const { vkDeviceWaitIdle(vkbDevice_.device); }

private:
    vkb::Instance vkbInstance_{};
    vkb::Device   vkbDevice_{};

    VkQueue        graphicsQueue_       = VK_NULL_HANDLE;
    uint32_t       graphicsQueueFamily_ = 0;
    vma::Allocator allocator_           = nullptr;

    std::string gpuName_;

    PFN_vkCmdDrawMeshTasksEXT              cmdDrawMeshTasks_             = nullptr;
    PFN_vkCmdDrawMeshTasksIndirectCountEXT cmdDrawMeshTasksIndirectCount_ = nullptr;
#ifdef _WIN32
    PFN_vkGetMemoryWin32HandleKHR    getMemoryWin32Handle_    = nullptr;
    PFN_vkGetSemaphoreWin32HandleKHR getSemaphoreWin32Handle_ = nullptr;
#endif

    bool createInstance(const char* appName);
    bool selectAndCreateDevice(std::span<const uint8_t> preferredDeviceUuid);
    bool createAllocator();
    bool loadDeviceExtensionFunctions();
};
