module;
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#endif
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>

#include <cstring>
#include <span>
#include <string>

module VulkanContext;

import vulkan;
import vk_mem_alloc;
import Logger;

namespace {

bool ok(vk::Result r, const char* what) {
    if (r == vk::Result::eSuccess) return true;
    logError(std::string(what) + " failed: " + vk::to_string(r));
    return false;
}

} // namespace

bool VulkanContext::init(const char* appName, std::span<const uint8_t> preferredDeviceUuid) {
    if (!createInstance(appName))
        return false;

    if (!selectAndCreateDevice(preferredDeviceUuid))
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

    cmdDrawMeshTasksIndirectCount_ =
        reinterpret_cast<PFN_vkCmdDrawMeshTasksIndirectCountEXT>(
            vkGetDeviceProcAddr(vkbDevice_.device, "vkCmdDrawMeshTasksIndirectCountEXT"));
    if (!cmdDrawMeshTasksIndirectCount_) {
        logError("vkGetDeviceProcAddr: vkCmdDrawMeshTasksIndirectCountEXT not found "
                 "although VK_EXT_mesh_shader was required");
        return false;
    }

#ifdef _WIN32
    getMemoryWin32Handle_ =
        reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(
            vkGetDeviceProcAddr(vkbDevice_.device, "vkGetMemoryWin32HandleKHR"));
    if (!getMemoryWin32Handle_) {
        logError("vkGetDeviceProcAddr: vkGetMemoryWin32HandleKHR not found");
        return false;
    }

    getSemaphoreWin32Handle_ =
        reinterpret_cast<PFN_vkGetSemaphoreWin32HandleKHR>(
            vkGetDeviceProcAddr(vkbDevice_.device, "vkGetSemaphoreWin32HandleKHR"));
    if (!getSemaphoreWin32Handle_) {
        logError("vkGetDeviceProcAddr: vkGetSemaphoreWin32HandleKHR not found");
        return false;
    }
#else
    throw std::logic_error("This OS support is not implemented");
#endif
    return true;
}

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debugMessengerCB(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*) {

    if (!data || !data->pMessage) return VK_FALSE;

    switch (severity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            logError(data->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            logWarning(data->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            logMessage(data->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        default:
            break;
    }

    return VK_FALSE;
}

} // namespace

bool VulkanContext::createInstance(const char* appName) {
    vkb::InstanceBuilder builder;
    builder.set_app_name(appName)
           .set_headless(true)
           .require_api_version(1, 3, 0);
#ifndef NDEBUG
    builder.request_validation_layers(true)
           .set_debug_callback(debugMessengerCB);
#endif

    auto instRet = builder.build();
    if (!instRet) {
        logError("vkb instance: " + instRet.error().message());
        return false;
    }
    vkbInstance_ = instRet.value();
    return true;
}

bool VulkanContext::selectAndCreateDevice(std::span<const uint8_t> preferredDeviceUuid) {
    VkPhysicalDeviceFeatures features10{};
    features10.multiDrawIndirect        = VK_TRUE;  // >1 draw per indirect call
    features10.drawIndirectFirstInstance = VK_TRUE; // per-draw instance offset
    features10.shaderInt64              = VK_TRUE;  // 64-bit math on buffer pointers
    features10.shaderInt16              = VK_TRUE;  // uint16_t arithmetic on Meshlet counts
    features10.textureCompressionBC     = VK_TRUE;  // the BC7 the baker writes
    features10.geometryShader           = VK_TRUE;  // SV_PrimitiveID in the visibility-buffer fragment shader
    // One RWTexture2D[] array holds storage images of every format.
    features10.shaderStorageImageReadWithoutFormat  = VK_TRUE;
    features10.shaderStorageImageWriteWithoutFormat = VK_TRUE;

    VkPhysicalDeviceVulkan11Features features11{};
    features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    // gl_DrawID in the mesh shader, which indexes the per-draw GPUDrawData.
    features11.shaderDrawParameters = VK_TRUE;
    // uint16_t fields of Meshlet read through buffer pointers.
    features11.storageBuffer16BitAccess = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.scalarBlockLayout   = VK_TRUE;   // C-like struct layout in shaders
    features12.timelineSemaphore   = VK_TRUE;
    // uint8_t fields of Triangle read through buffer pointers.
    features12.storageBuffer8BitAccess = VK_TRUE;
    features12.shaderInt8              = VK_TRUE;
    features12.storagePushConstant8    = VK_TRUE;
    // Half-float texture coordinates of Vertex.
    features12.shaderFloat16           = VK_TRUE;
    // Draw count sourced from a GPU buffer - the culling compute pass writes it.
    features12.drawIndirectCount   = VK_TRUE;

    features12.descriptorIndexing                              = VK_TRUE;
    features12.runtimeDescriptorArray                          = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing       = VK_TRUE;
    features12.shaderStorageBufferArrayNonUniformIndexing      = VK_TRUE;
    features12.shaderStorageImageArrayNonUniformIndexing       = VK_TRUE;
    features12.descriptorBindingPartiallyBound                 = VK_TRUE;
    features12.descriptorBindingVariableDescriptorCount        = VK_TRUE;
    features12.descriptorBindingSampledImageUpdateAfterBind    = VK_TRUE;
    features12.descriptorBindingStorageBufferUpdateAfterBind   = VK_TRUE;
    features12.descriptorBindingStorageImageUpdateAfterBind    = VK_TRUE;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;   // no VkRenderPass/VkFramebuffer
    features13.synchronization2 = VK_TRUE;   // the barrier2 API
    /**
     * Required for OpExecutionMode LocalSizeId, which SPIR-V 1.6 uses for
     * compute/mesh workgroup size.
     */
    features13.maintenance4     = VK_TRUE;
    /* Slang lowers `discard` to OpDemoteToHelperInvocation. */
    features13.shaderDemoteToHelperInvocation = VK_TRUE;

    VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures{};
    meshFeatures.sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    meshFeatures.taskShader = VK_TRUE;
    meshFeatures.meshShader = VK_TRUE;

    vkb::PhysicalDeviceSelector selector{vkbInstance_};
    selector.set_minimum_version(1, 3)
            .add_required_extension(VK_EXT_MESH_SHADER_EXTENSION_NAME)
#ifdef _WIN32
            .add_required_extension(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME)
            .add_required_extension(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME)
#endif
            .set_required_features(features10)
            .set_required_features_11(features11)
            .set_required_features_12(features12)
            .set_required_features_13(features13)
            .add_required_extension_features(meshFeatures);

    vkb::PhysicalDevice chosen{};
    if (preferredDeviceUuid.size() == VK_UUID_SIZE) {
        auto devicesRet = selector.select_devices();
        if (!devicesRet || devicesRet.value().empty()) {
            logError("vkb physical device: " + devicesRet.error().message());
            return false;
        }
        bool matched = false;
        for (const vkb::PhysicalDevice& candidate : devicesRet.value()) {
            VkPhysicalDeviceIDProperties idProps{};
            idProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
            VkPhysicalDeviceProperties2 props2{};
            props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            props2.pNext = &idProps;
            vkGetPhysicalDeviceProperties2(candidate.physical_device, &props2);
            if (std::memcmp(idProps.deviceUUID, preferredDeviceUuid.data(), VK_UUID_SIZE) == 0) {
                chosen = candidate;
                matched = true;
                break;
            }
        }
        if (!matched) {
            logError("VulkanContext: preferred host GPU UUID not found; using first compatible GPU");
            chosen = devicesRet.value().front();
        }
    } else {
        auto physRet = selector.select();
        if (!physRet) {
            logError("vkb physical device: " + physRet.error().message());
            return false;
        }
        chosen = physRet.value();
    }

    gpuName_ = chosen.properties.deviceName;
    logMessage("GPU: " + gpuName_);

    auto devRet = vkb::DeviceBuilder{chosen}.build();
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
    vma::AllocatorCreateInfo info{};
    info.physicalDevice   = vkbDevice_.physical_device;
    info.device           = vkbDevice_.device;
    info.instance         = vkbInstance_.instance;
    info.vulkanApiVersion = VK_API_VERSION_1_3;
    /* Must match the device feature; VMA needs it for address-taken memory. */
    info.flags            = vma::AllocatorCreateFlagBits::eBufferDeviceAddress;

    return ok(vma::createAllocator(&info, &allocator_), "vma::createAllocator");
}

void VulkanContext::shutdown() {
    if (allocator_) {
        allocator_.destroy();
        allocator_ = nullptr;
    }
    if (vkbDevice_.device) {
        vkb::destroy_device(vkbDevice_);
        vkbDevice_ = {};
    }
    if (vkbInstance_.instance) {
        vkb::destroy_instance(vkbInstance_);
        vkbInstance_ = {};
    }
}
