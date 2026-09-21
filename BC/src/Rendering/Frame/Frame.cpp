module;
#include <vulkan/vulkan_core.h>

#include <exception>
#include <stdexcept>
#include <string>

module Frame;

import vulkan;
import VkUtil;
import Logger;

Frame::Frame(VulkanContext& ctx, vk::Image image, vk::Format format, vk::Extent2D extent)
    : device_(ctx.device()), queue_(ctx.graphicsQueue()), frameColor_(device_, image, format, extent) {
    if (!image)
        throw std::invalid_argument("Frame: image is required");

    vk::CommandPoolCreateInfo pool{};
    pool.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    pool.queueFamilyIndex = ctx.graphicsQueueFamily();
    commandPool_ = device_.createCommandPoolUnique(pool);

    vk::CommandBufferAllocateInfo allocate{};
    allocate.commandPool = *commandPool_;
    allocate.level = vk::CommandBufferLevel::ePrimary;
    allocate.commandBufferCount = 1;
    commandBuffer_ = device_.allocateCommandBuffers(allocate).front();

    completion_ = device_.createFenceUnique({vk::FenceCreateFlagBits::eSignaled});
    renderFinished_ = device_.createSemaphoreUnique({});
    if (!frameColor_.view())
        throw std::runtime_error("Frame: color attachment view creation failed");
}

Frame::~Frame() {
    if (state_ == State::Recording) {
        logError("Frame: destruction during its recording callback");
        std::terminate();
    }
    if (state_ != State::Ready) {
        const auto result = static_cast<vk::Result>(vkDeviceWaitIdle(static_cast<VkDevice>(device_)));
        if (result != vk::Result::eSuccess && result != vk::Result::eErrorDeviceLost) {
            logError("Frame: device wait during destruction failed: " + vk::to_string(result));
            std::terminate();
        }
    }
}

void Frame::require(State expected, const char* operation) const {
    if (state_ != expected)
        throw std::logic_error(std::string("Frame::") + operation + ": invalid image lifecycle state");
}

bool Frame::failed() const {
    return state_ == State::Failed;
}

void Frame::acceptAcquisition(vk::Semaphore imageAvailable) {
    if (!imageAvailable)
        throw std::invalid_argument("Frame::acceptAcquisition: acquisition semaphore is required");
    if (state_ != State::Ready && state_ != State::Released)
        throw std::logic_error("Frame::acceptAcquisition: image cannot accept acquisition");

    imageAvailable_ = imageAvailable;
    state_ = State::Acquired;
}

vk::Result Frame::completionStatus() const {
    return submitted_ ? device_.getFenceStatus(*completion_) : vk::Result::eSuccess;
}

vk::Result Frame::waitUntilComplete() const {
    if (!submitted_) return vk::Result::eSuccess;
    const vk::Fence fence = *completion_;
    return device_.waitForFences(1, &fence, vk::True, UINT64_MAX);
}

vk::CommandBuffer Frame::Recording::commandBuffer() const {
    frame_.require(State::Recording, "Recording::commandBuffer");
    return frame_.commandBuffer_;
}

vk::Extent2D Frame::Recording::extent() const {
    frame_.require(State::Recording, "Recording::extent");
    return frame_.frameColor_.extent2D();
}

void Frame::transition(ImageUse imageUse) {
    require(State::Recording, "transition");

    transitionImage(commandBuffer_, frameColor_.image().handle(), recordingUse_.layout, imageUse.layout,
                    recordingUse_.stageMask, recordingUse_.accessMask,
                    imageUse.stageMask, imageUse.accessMask);
    recordingUse_ = imageUse;
}

vk::RenderingAttachmentInfo Frame::Recording::colorAttachment(vk::ClearColorValue clearColor) {
    const auto colorAccess = vk::AccessFlagBits2::eColorAttachmentRead |
                             vk::AccessFlagBits2::eColorAttachmentWrite;
    frame_.transition({vk::ImageLayout::eColorAttachmentOptimal,
                       vk::PipelineStageFlagBits2::eColorAttachmentOutput, colorAccess});

    vk::RenderingAttachmentInfo color{};
    color.imageView = frame_.frameColor_.view().get();
    color.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    color.loadOp = vk::AttachmentLoadOp::eClear;
    color.storeOp = vk::AttachmentStoreOp::eStore;
    color.clearValue.color = clearColor;
    return color;
}

vk::Result Frame::beginRecording() {
    vk::Result result = waitUntilComplete();
    if (result != vk::Result::eSuccess) return result;
    submitted_ = false;

    result = static_cast<vk::Result>(vkResetCommandBuffer(
        static_cast<VkCommandBuffer>(commandBuffer_), 0));
    if (result != vk::Result::eSuccess) return result;
    vk::CommandBufferBeginInfo begin{};
    result = commandBuffer_.begin(&begin);
    if (result != vk::Result::eSuccess) return result;

    state_ = State::Recording;
    recordingUse_ = imageUse_;
    return vk::Result::eSuccess;
}

vk::Result Frame::finishRecording(ImageUse imageUse) {
    transition(imageUse);
    return static_cast<vk::Result>(vkEndCommandBuffer(static_cast<VkCommandBuffer>(commandBuffer_)));
}

vk::Result Frame::submit(Submission submission) {
    vk::SemaphoreSubmitInfo wait{};
    wait.semaphore = imageAvailable_;
    wait.stageMask = vk::PipelineStageFlagBits2::eAllCommands;

    vk::SemaphoreSubmitInfo signals[2]{};
    signals[0].semaphore = *renderFinished_;
    signals[0].stageMask = vk::PipelineStageFlagBits2::eAllCommands;
    signals[1].semaphore = submission.timeline;
    signals[1].value = submission.serial;
    signals[1].stageMask = vk::PipelineStageFlagBits2::eAllCommands;

    vk::CommandBufferSubmitInfo commands{};
    commands.commandBuffer = commandBuffer_;
    vk::SubmitInfo2 submitInfo{};
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &wait;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commands;
    submitInfo.signalSemaphoreInfoCount = 2;
    submitInfo.pSignalSemaphoreInfos = signals;

    const vk::Fence fence = *completion_;
    vk::Result result = device_.resetFences(1, &fence);
    if (result != vk::Result::eSuccess) return result;
    return queue_.submit2(1, &submitInfo, fence);
}

vk::Result Frame::recordAndSubmit(const RecordFn& record,
                                  FrameInFlightIndex frameInFlight,
                                  Submission submission) {
    require(State::Acquired, "recordAndSubmit");
    if (!submission.timeline || submission.serial == 0 ||
        submission.finalImageUse.layout == vk::ImageLayout::eUndefined)
        throw std::invalid_argument("Frame::recordAndSubmit: submission signals and final image use are required");

    const auto fail = [this](vk::Result result) {
        state_ = State::Failed;
        return result;
    };
    try {
        vk::Result result = beginRecording();
        if (result != vk::Result::eSuccess) return fail(result);

        Recording recording{*this, frameInFlight};
        if (record) record(recording);
        result = finishRecording(submission.finalImageUse);
        if (result != vk::Result::eSuccess) return fail(result);

        result = submit(submission);
        if (result != vk::Result::eSuccess) return fail(result);

        submitted_ = true;
        imageUse_ = recordingUse_;
        state_ = State::Submitted;
        imageAvailable_ = nullptr;
        return vk::Result::eSuccess;
    } catch (...) {
        state_ = State::Failed;
        throw;
    }
}

vk::Semaphore Frame::release() {
    require(State::Submitted, "release");
    state_ = State::Released;
    return *renderFinished_;
}
