module;

#include <array>
#include <cstdint>
#include <format>

module GpuPassTimings;

import vulkan;
import Logger;
import Metrics;

namespace {

/// e64 with eWithAvailability writes the value and its availability per query.
constexpr vk::DeviceSize RESULT_STRIDE = 2 * sizeof(uint64_t);

constexpr float NANOSECONDS_PER_MILLISECOND = 1.0e6f;

} // namespace

bool GpuPassTimings::init(VulkanContext& ctx) {
    const vk::PhysicalDevice physicalDevice = ctx.physicalDevice();
    const auto families = physicalDevice.getQueueFamilyProperties();
    if (families[ctx.graphicsQueueFamily()].timestampValidBits == 0) {
        logError("GpuPassTimings: the graphics queue family writes no timestamps");
        return false;
    }

    nanosecondsPerTick_ = physicalDevice.getProperties().limits.timestampPeriod;
    device_ = ctx.device();

    vk::QueryPoolCreateInfo info{};
    info.queryType  = vk::QueryType::eTimestamp;
    info.queryCount = MAX_MARKS;
    for (Slot& slot : slots_) {
        if (device_.createQueryPool(&info, nullptr, &slot.pool) !=
            vk::Result::eSuccess) {
            logError("GpuPassTimings: vkCreateQueryPool failed");
            destroy();
            return false;
        }
    }
    return true;
}

void GpuPassTimings::destroy() {
    if (!device_) return;
    for (Slot& slot : slots_) {
        if (slot.pool) device_.destroyQueryPool(slot.pool);
        slot.pool = nullptr;
        slot.markCount = 0;
    }
    current_ = nullptr;
    device_  = nullptr;
}

void GpuPassTimings::publish(Slot& slot) {
    if (slot.markCount < 2) return;

    std::array<uint64_t, MAX_MARKS * 2> raw{};
    const vk::Result result = device_.getQueryPoolResults(
        slot.pool, 0, slot.markCount, RESULT_STRIDE * slot.markCount, raw.data(),
        RESULT_STRIDE,
        vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability);
    if (result != vk::Result::eSuccess && result != vk::Result::eNotReady) {
        logError(std::format("GpuPassTimings: vkGetQueryPoolResults returned {}",
                             vk::to_string(result)));
        return;
    }

    timingCount_ = 0;
    for (uint32_t mark = 1; mark < slot.markCount; ++mark) {
        if (raw[(mark - 1) * 2 + 1] == 0 || raw[mark * 2 + 1] == 0) continue;
        const uint64_t ticks = raw[mark * 2] - raw[(mark - 1) * 2];
        const Timing timing{slot.names[mark],
                            static_cast<float>(ticks) * nanosecondsPerTick_ /
                                NANOSECONDS_PER_MILLISECOND};
        timings_[timingCount_++] = timing;
        Metrics::get().set(timing.name, timing.milliseconds);
    }
}

void GpuPassTimings::beginFrame(Frame::Recording& recording) {
    if (!device_) return;

    Slot& slot = recording.select(slots_);
    publish(slot);

    const vk::CommandBuffer commandBuffer = recording.commandBuffer();
    commandBuffer.resetQueryPool(slot.pool, 0, MAX_MARKS);
    slot.markCount = 0;
    current_ = &slot;
    mark(commandBuffer, nullptr);
}

void GpuPassTimings::mark(vk::CommandBuffer commandBuffer, const char* name) {
    if (!current_ || current_->markCount >= MAX_MARKS) return;

    current_->names[current_->markCount] = name;
    commandBuffer.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands,
                                  current_->pool, current_->markCount);
    ++current_->markCount;
}
