module;
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <string>

module VulkanContext;

import Logger;

namespace {

bool ok(VkResult r, const char* what) {
    if (r == VK_SUCCESS) return true;
    logError(std::string(what) + " failed: VkResult " + std::to_string(r));
    return false;
}

} // namespace


bool VulkanContext::init(const AppWindow& window, const char* appName) {
    if (!createInstance(appName))
        return false;

    if (!window.createSurface(vkbInstance_.instance, &surface_))
        return false;

    if (!selectAndCreateDevice())
        return false;

    if (!loadDeviceExtensionFunctions())
        return false;

    return createAllocator();
}

bool VulkanContext::loadDeviceExtensionFunctions() {
    cmdDrawMeshTasks_ = reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(
        vkGetDeviceProcAddr(vkbDevice_.device, "vkCmdDrawMeshTasksEXT"));
    if (!cmdDrawMeshTasks_) {
        logError("vkGetDeviceProcAddr: vkCmdDrawMeshTasksEXT not found although "
                 "VK_EXT_mesh_shader was required");
        return false;
    }
    return true;
}

bool VulkanContext::createInstance(const char* appName) {
    vkb::InstanceBuilder builder;
    builder.set_app_name(appName)
           .require_api_version(1, 3, 0);
#ifndef NDEBUG
    builder.request_validation_layers(true)
           .use_default_debug_messenger();
#endif

    auto instRet = builder.build();
    if (!instRet) {
        logError("vkb instance: " + instRet.error().message());
        return false;
    }
    vkbInstance_ = instRet.value();
    return true;
}

bool VulkanContext::selectAndCreateDevice() {
    VkPhysicalDeviceFeatures features10{};
    features10.multiDrawIndirect        = VK_TRUE;  // >1 draw per indirect call
    features10.drawIndirectFirstInstance = VK_TRUE; // per-draw instance offset
    features10.shaderInt64              = VK_TRUE;  // 64-bit math on buffer pointers

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.scalarBlockLayout   = VK_TRUE;   // C-like struct layout in shaders
    features12.timelineSemaphore   = VK_TRUE;
    // Draw count sourced from a GPU buffer - the culling compute pass writes it.
    features12.drawIndirectCount   = VK_TRUE;

    features12.descriptorIndexing                              = VK_TRUE;
    features12.runtimeDescriptorArray                          = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing       = VK_TRUE;
    features12.shaderStorageBufferArrayNonUniformIndexing      = VK_TRUE;
    features12.descriptorBindingPartiallyBound                 = VK_TRUE;
    features12.descriptorBindingVariableDescriptorCount        = VK_TRUE;
    features12.descriptorBindingSampledImageUpdateAfterBind    = VK_TRUE;
    features12.descriptorBindingStorageBufferUpdateAfterBind   = VK_TRUE;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;   // no VkRenderPass/VkFramebuffer
    features13.synchronization2 = VK_TRUE;   // the barrier2 API
    /**
     * Required for OpExecutionMode LocalSizeId, which SPIR-V 1.6 uses for
     * compute/mesh workgroup size.
     */
    features13.maintenance4     = VK_TRUE;

    VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures{};
    meshFeatures.sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    meshFeatures.taskShader = VK_TRUE;
    meshFeatures.meshShader = VK_TRUE;

    vkb::PhysicalDeviceSelector selector{vkbInstance_};
    auto physRet = selector.set_surface(surface_)
                           .set_minimum_version(1, 3)
                           .add_required_extension(VK_EXT_MESH_SHADER_EXTENSION_NAME)
                           .set_required_features(features10)
                           .set_required_features_12(features12)
                           .set_required_features_13(features13)
                           .add_required_extension_features(meshFeatures)
                           .select();
    if (!physRet) {
        logError("vkb physical device: " + physRet.error().message());
        return false;
    }

    gpuName_ = physRet.value().properties.deviceName;
    logMessage("GPU: " + gpuName_);

    auto devRet = vkb::DeviceBuilder{physRet.value()}.build();
    if (!devRet) {
        logError("vkb device: " + devRet.error().message());
        return false;
    }
    vkbDevice_ = devRet.value();

    auto queueRet = vkbDevice_.get_queue(vkb::QueueType::graphics);
    if (!queueRet) {
        logError("no graphics queue: " + queueRet.error().message());
        return false;
    }
    graphicsQueue_       = queueRet.value();
    graphicsQueueFamily_ = vkbDevice_.get_queue_index(vkb::QueueType::graphics).value();
    return true;
}

bool VulkanContext::createAllocator() {
    VmaAllocatorCreateInfo info{};
    info.physicalDevice   = vkbDevice_.physical_device;
    info.device           = vkbDevice_.device;
    info.instance         = vkbInstance_.instance;
    info.vulkanApiVersion = VK_API_VERSION_1_3;
    /* Must match the device feature; VMA needs it for address-taken memory. */
    info.flags            = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    return ok(vmaCreateAllocator(&info, &allocator_), "vmaCreateAllocator");
}

void VulkanContext::shutdown() {
    if (allocator_) {
        vmaDestroyAllocator(allocator_);
        allocator_ = nullptr;
    }
    if (vkbDevice_.device) {
        vkb::destroy_device(vkbDevice_);
        vkbDevice_ = {};
    }
    if (surface_) {
        vkb::destroy_surface(vkbInstance_, surface_);
        surface_ = VK_NULL_HANDLE;
    }
    if (vkbInstance_.instance) {
        vkb::destroy_instance(vkbInstance_);
        vkbInstance_ = {};
    }
}
