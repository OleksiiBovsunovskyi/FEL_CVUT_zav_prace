module;
#include <vulkan/vulkan.hpp>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

module FrameRunner;

import Logger;

bool FrameRunner::checkResult(vk::Result result, const char* operation) {
    if (result == vk::Result::eSuccess) return true;
    if (result == vk::Result::eErrorDeviceLost) {
        if (!deviceLost_)
            logError(std::string("FrameRunner: device lost during ") + operation);
        deviceLost_ = true;
        return false;
    }
    logError(std::string("FrameRunner: ") + operation + " failed: " + vk::to_string(result));
    return false;
}

bool FrameRunner::checkFatal(vk::Result result, const char* operation) {
    if (checkResult(result, operation)) return true;
    if (deviceLost_) return false;
    throw std::runtime_error(std::string("FrameRunner: ") + operation +
                             " failed: " + vk::to_string(result));
}

bool FrameRunner::checkImageCount() const {
    if (swapchain_->imageCount() < FRAMES_IN_FLIGHT) {
        logError("FrameRunner: swapchain image count is below FRAMES_IN_FLIGHT");
        return false;
    }
    return true;
}

bool FrameRunner::init(VulkanContext& ctx, Swapchain& swapchain) {
    swapchain_ = &swapchain;
    device_ = ctx.device();
    return checkImageCount() && createSyncObjects();
}

bool FrameRunner::createSyncObjects() {
    vk::SemaphoreCreateInfo semaphore{};
    for (auto& available : imageAvailable_) {
        if (!checkResult(device_.createSemaphore(&semaphore, nullptr, &available),
                         "create acquisition semaphore"))
            return false;
    }

    vk::SemaphoreTypeCreateInfo type{};
    type.semaphoreType = vk::SemaphoreType::eTimeline;
    type.initialValue = 0;
    semaphore.pNext = &type;
    return checkResult(device_.createSemaphore(&semaphore, nullptr, &timeline_),
                       "create submission timeline");
}

FrameRunner::SubmissionSerial FrameRunner::getCompletedSerial() const {
    if (!timeline_) return 0;
    SubmissionSerial value = 0;
    if (device_.getSemaphoreCounterValue(timeline_, &value) != vk::Result::eSuccess)
        return 0;
    return value;
}

bool FrameRunner::waitForSerial(SubmissionSerial serial, uint64_t timeoutNs) const {
    if (!timeline_) return false;
    if (serial == 0) return true;
    vk::SemaphoreWaitInfo wait{};
    wait.semaphoreCount = 1;
    wait.pSemaphores = &timeline_;
    wait.pValues = &serial;
    return device_.waitSemaphores(&wait, timeoutNs) == vk::Result::eSuccess;
}

bool FrameRunner::recreateSwapchain() const
{
    if (!swapchain_->recreate() || !checkImageCount())
        throw std::runtime_error("FrameRunner: swapchain recreation failed");
    if (onSwapchainRecreated_)
        onSwapchainRecreated_(swapchain_->extent(), swapchain_->format());
    return true;
}

void FrameRunner::drawFrame(const RecordFn& record) {
    if (deviceLost_) return;

    SubmissionSerial& frameSubmission = currentFrame_.select(frameSubmissions_);
    vk::Semaphore& imageAvailable = currentFrame_.select(imageAvailable_);

    vk::SemaphoreWaitInfo wait{};
    wait.semaphoreCount = 1;
    wait.pSemaphores = &timeline_;
    wait.pValues = &frameSubmission;
    if (!checkFatal(device_.waitSemaphores(&wait, UINT64_MAX), "wait for per-frame resources"))
        return;

    const Swapchain::Acquisition acquired = swapchain_->acquire(imageAvailable);
    if (acquired.result == vk::Result::eErrorOutOfDateKHR) {
        recreateSwapchain();
        framebufferResized_ = false;
        return;
    }
    if (acquired.result != vk::Result::eSuccess && acquired.result != vk::Result::eSuboptimalKHR) {
        checkFatal(acquired.result, "acquire image");
        return;
    }

    const SubmissionSerial serial = submittedSerial_ + 1;
    constexpr Frame::ImageUse finalImageUse{vk::ImageLayout::ePresentSrcKHR,
                                        vk::PipelineStageFlagBits2::eNone, {}};
    if (!checkFatal(acquired.frame->recordAndSubmit(
                        record, currentFrame_, {timeline_, serial, finalImageUse}),
                    "record and submit Frame"))
        return;
    submittedSerial_ = serial;
    frameSubmission = serial;

    const vk::Result present = swapchain_->present(*acquired.frame);
    if (present != vk::Result::eSuccess && present != vk::Result::eSuboptimalKHR &&
        present != vk::Result::eErrorOutOfDateKHR) {
        checkFatal(present, "present Frame");
        return;
    }
    if (present == vk::Result::eErrorOutOfDateKHR || present == vk::Result::eSuboptimalKHR ||
        acquired.result == vk::Result::eSuboptimalKHR || framebufferResized_) {
        if (!recreateSwapchain())
            throw std::runtime_error("FrameRunner: swapchain recreation failed");
        framebufferResized_ = false;
    }
    currentFrame_ = currentFrame_.next();
}

void FrameRunner::destroy() {
    if (!device_) return;
    const auto result = static_cast<vk::Result>(vkDeviceWaitIdle(static_cast<VkDevice>(device_)));
    checkResult(result, "wait before synchronization destruction");
    for (vk::Semaphore semaphore : imageAvailable_)
        device_.destroySemaphore(semaphore);
    imageAvailable_.fill(nullptr);
    device_.destroySemaphore(timeline_);
    timeline_ = nullptr;
    frameSubmissions_.fill(0);
    submittedSerial_ = 0;
    currentFrame_ = FrameInFlightIndex::first();
    framebufferResized_ = false;
    deviceLost_ = false;
    device_ = nullptr;
    swapchain_ = nullptr;
}
