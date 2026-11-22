module;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#endif
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

module ExternalTarget;

import vulkan;
import Logger;

namespace {

void closeHandle(void*& handle) {
#ifdef _WIN32
    if (handle) {
        CloseHandle(static_cast<HANDLE>(handle));
        handle = nullptr;
    }
#else
    handle = nullptr;
    throw std::logic_error("This OS support is not implemented");
#endif
}

} // namespace

bool ExternalTarget::init(VulkanContext& ctx, PresentationBridge& bridge, vk::Extent2D extent) {
    ctx_    = &ctx;
    bridge_ = &bridge;
    extent_ = extent;
    return build();
}

uint32_t ExternalTarget::find_device_memory_type(uint32_t type_bits) const {
    const vk::PhysicalDeviceMemoryProperties props = ctx_->physicalDevice().getMemoryProperties();
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        const bool allowed = (type_bits & (1u << i)) != 0;
        const bool deviceLocal =
            (props.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal) ==
            vk::MemoryPropertyFlagBits::eDeviceLocal;
        if (allowed && deviceLocal) return i;
    }
    throw std::runtime_error("ExternalTarget: no device-local memory type matches image requirements");
}

bool ExternalTarget::build() {
#ifndef _WIN32
    logError("ExternalTarget: Win32 external memory handles are required");
    return false;
#else
    const vk::Device device = ctx_->device();
    const VkDevice rawDevice = static_cast<VkDevice>(device);

    VkExternalMemoryImageCreateInfo externalImageInfo{};
    externalImageInfo.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    externalImageInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.pNext         = &externalImageInfo;
    imageInfo.flags         = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.format        = static_cast<VkFormat>(IMAGE_FORMAT);
    imageInfo.extent        = VkExtent3D{extent_.width, extent_.height, 1};
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkExportSemaphoreCreateInfo exportSemInfo{};
    exportSemInfo.sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    exportSemInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semInfo.pNext = &exportSemInfo;

    std::vector<PgrExternalSlotHandles> exportedHandles;
    exportedHandles.reserve(IMAGE_COUNT);
    slots_.reserve(IMAGE_COUNT);

    for (uint32_t i = 0; i < IMAGE_COUNT; ++i) {
        Slot slot{};

        VkImage rawImage = VK_NULL_HANDLE;
        if (vkCreateImage(rawDevice, &imageInfo, nullptr, &rawImage) != VK_SUCCESS) {
            logError("ExternalTarget: vkCreateImage failed");
            destroy_images();
            return false;
        }
        slot.image = rawImage;

        VkMemoryRequirements memReqs{};
        vkGetImageMemoryRequirements(rawDevice, rawImage, &memReqs);
        slot.memorySize = memReqs.size;

        VkMemoryDedicatedAllocateInfo dedicatedInfo{};
        dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicatedInfo.image = rawImage;

        VkExportMemoryAllocateInfo exportMemInfo{};
        exportMemInfo.sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
        exportMemInfo.pNext       = &dedicatedInfo;
        exportMemInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext           = &exportMemInfo;
        allocInfo.allocationSize  = memReqs.size;
        allocInfo.memoryTypeIndex = find_device_memory_type(memReqs.memoryTypeBits);

        VkDeviceMemory rawMemory = VK_NULL_HANDLE;
        if (vkAllocateMemory(rawDevice, &allocInfo, nullptr, &rawMemory) != VK_SUCCESS) {
            logError("ExternalTarget: vkAllocateMemory failed");
            slots_.push_back(std::move(slot));
            destroy_images();
            return false;
        }
        slot.memory = rawMemory;

        if (vkBindImageMemory(rawDevice, rawImage, rawMemory, 0) != VK_SUCCESS) {
            logError("ExternalTarget: vkBindImageMemory failed");
            slots_.push_back(std::move(slot));
            destroy_images();
            return false;
        }

        VkMemoryGetWin32HandleInfoKHR memHandleInfo{};
        memHandleInfo.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
        memHandleInfo.memory     = rawMemory;
        memHandleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        HANDLE memHandle = nullptr;
        if (ctx_->getMemoryWin32Handle()(rawDevice, &memHandleInfo, &memHandle) != VK_SUCCESS) {
            logError("ExternalTarget: vkGetMemoryWin32HandleKHR failed");
            slots_.push_back(std::move(slot));
            destroy_images();
            return false;
        }
        slot.memoryHandle = memHandle;

        auto createExportedSemaphore = [&](vk::Semaphore& outSem, void*& outHandle) -> bool {
            VkSemaphore rawSem = VK_NULL_HANDLE;
            if (vkCreateSemaphore(rawDevice, &semInfo, nullptr, &rawSem) != VK_SUCCESS)
                return false;
            outSem = rawSem;

            VkSemaphoreGetWin32HandleInfoKHR semHandleInfo{};
            semHandleInfo.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
            semHandleInfo.semaphore  = rawSem;
            semHandleInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
            HANDLE semHandle = nullptr;
            if (ctx_->getSemaphoreWin32Handle()(rawDevice, &semHandleInfo, &semHandle) != VK_SUCCESS)
                return false;
            outHandle = semHandle;
            return true;
        };

        if (!createExportedSemaphore(slot.renderDone, slot.renderDoneHandle) ||
            !createExportedSemaphore(slot.avaloniaDone, slot.avaloniaDoneHandle)) {
            logError("ExternalTarget: exportable semaphore creation failed");
            slots_.push_back(std::move(slot));
            destroy_images();
            return false;
        }

        slot.frame = std::make_unique<Frame>(*ctx_, slot.image, VIEW_FORMAT, extent_);

        exportedHandles.push_back(PgrExternalSlotHandles{
            .memoryHandle                = slot.memoryHandle,
            .memorySize                  = static_cast<uint64_t>(slot.memorySize),
            .memoryOffset                = 0,
            .renderDoneSemaphoreHandle   = slot.renderDoneHandle,
            .avaloniaDoneSemaphoreHandle = slot.avaloniaDoneHandle,
        });
        slots_.push_back(std::move(slot));
    }

    bridge_->resetSlots(IMAGE_COUNT);
    const PgrTargetSet targetSet{
        .slotCount = IMAGE_COUNT,
        .width     = extent_.width,
        .height    = extent_.height,
        .slots     = exportedHandles.data(),
    };
    bridge_->notifyTargetsCreated(targetSet);
    return true;
#endif
}

void ExternalTarget::destroy_images() {
    if (bridge_ && !slots_.empty()) bridge_->notifyTargetsDestroyed();
    if (ctx_) ctx_->waitIdle();

    const vk::Device device = ctx_ ? ctx_->device() : vk::Device{};
    for (Slot& slot : slots_) {
        slot.frame.reset();
        if (device) {
            if (slot.renderDone) device.destroySemaphore(slot.renderDone);
            if (slot.avaloniaDone) device.destroySemaphore(slot.avaloniaDone);
            if (slot.image) device.destroyImage(slot.image);
            if (slot.memory) device.freeMemory(slot.memory);
        }
        closeHandle(slot.memoryHandle);
        closeHandle(slot.renderDoneHandle);
        closeHandle(slot.avaloniaDoneHandle);
    }
    slots_.clear();
}

void ExternalTarget::destroy() {
    if (!ctx_) return;
    destroy_images();
    bridge_ = nullptr;
    ctx_    = nullptr;
}

bool ExternalTarget::recreate() {
    ctx_->waitIdle();
    destroy_images();
    if (const std::optional<vk::Extent2D> requested = bridge_->requestedExtent())
        extent_ = *requested;
    return build();
}

Frame::ImageUse ExternalTarget::finalImageUse() const {
    return {vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eShaderRead};
}

PresentTarget::Acquisition ExternalTarget::acquire(vk::Semaphore imageAvailable) {
    if (!imageAvailable)
        throw std::invalid_argument("ExternalTarget::acquire: acquisition semaphore is required");
    if (slots_.empty())
        throw std::logic_error("ExternalTarget::acquire: no images exist");

    const std::optional<AcquiredSlot> acquired = bridge_->acquireSlot();
    if (!acquired) return {vk::Result::eNotReady, nullptr};

    Slot& slot = slots_[acquired->index];

    vk::SemaphoreSubmitInfo wait{};
    wait.semaphore = slot.avaloniaDone;
    wait.stageMask = vk::PipelineStageFlagBits2::eAllCommands;

    vk::SemaphoreSubmitInfo signal{};
    signal.semaphore = imageAvailable;
    signal.stageMask = vk::PipelineStageFlagBits2::eAllCommands;

    vk::SubmitInfo2 submit{};
    if (acquired->hasBeenPresented) {
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos    = &wait;
    }
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos    = &signal;

    const vk::Result result = ctx_->graphicsQueue().submit2(1, &submit, nullptr);
    if (result != vk::Result::eSuccess) return {result, nullptr};

    slot.frame->acceptAcquisition(imageAvailable);
    return {vk::Result::eSuccess, slot.frame.get()};
}

vk::Result ExternalTarget::present(Frame& frame) {
    uint32_t index = 0;
    while (index < slots_.size() && slots_[index].frame.get() != &frame) ++index;
    if (index == slots_.size())
        throw std::invalid_argument("ExternalTarget::present: Frame does not belong to this target");

    const vk::Semaphore rendered = frame.release();
    Slot& slot                   = slots_[index];

    vk::SemaphoreSubmitInfo wait{};
    wait.semaphore = rendered;
    wait.stageMask = vk::PipelineStageFlagBits2::eAllCommands;

    vk::SemaphoreSubmitInfo signal{};
    signal.semaphore = slot.renderDone;
    signal.stageMask = vk::PipelineStageFlagBits2::eAllCommands;

    vk::SubmitInfo2 submit{};
    submit.waitSemaphoreInfoCount   = 1;
    submit.pWaitSemaphoreInfos      = &wait;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos    = &signal;

    const vk::Result result = ctx_->graphicsQueue().submit2(1, &submit, nullptr);
    if (result != vk::Result::eSuccess) return result;

    bridge_->presentSlot(index);

    const std::optional<vk::Extent2D> requested = bridge_->requestedExtent();
    const bool resized = requested && (requested->width != extent_.width || requested->height != extent_.height);
    return resized ? vk::Result::eSuboptimalKHR : vk::Result::eSuccess;
}
