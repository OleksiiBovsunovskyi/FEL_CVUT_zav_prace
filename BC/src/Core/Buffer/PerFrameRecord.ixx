module;

#include <array>

export module PerFrameRecord;

import vulkan;
import AllocatedBuffer;
import BufferTypes;
import FrameInFlightIndex;
import GPUTypes;
import Logger;
import VulkanContext;

/**
 * Owns one T per frame-in-flight slot, written through the mapping.
 * @note T must have the layout its shader mirror declares.
 */
export template <typename T>
class PerFrameRecord {
public:
    PerFrameRecord() = default;
    ~PerFrameRecord() { destroy(); }

    PerFrameRecord(const PerFrameRecord&)            = delete;
    PerFrameRecord& operator=(const PerFrameRecord&) = delete;

    /**
     * Allocates one record per frame-in-flight slot.
     * @param ctx supplies the device and the VMA allocator.
     * @param debugName name reported by VMA; 
     * @return false on a failed allocation.
     */
    [[nodiscard]] bool init(VulkanContext& ctx, const char* debugName) {
        constexpr vk::BufferUsageFlags usage =
            vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eShaderDeviceAddress;

        for (auto& buffer : buffers_) {
            if (!buffer.init(ctx, sizeof(T), usage, debugName)) {
                destroy();
                return false;
            }
        }
        return true;
    }

    void destroy() {
        for (auto& buffer : buffers_) buffer.destroy();
    }

    /**
     * Writes one slot's record.
     * @param frameInFlight slot the record is written into.
     * @param record value to write.
     * @return the record's address, null before PerFrameRecord::init().
     * @note The slot's previous submission must have completed.
     */
    [[nodiscard]] GpuPtr<T> write(FrameInFlightIndex frameInFlight, const T& record) {
        AllocatedBuffer<DeviceHostMappedBuffer>& buffer =
            frameInFlight.select(buffers_);
        if (!buffer.write(&record, 1)) {
            logError("PerFrameRecord::write: no buffer to write into");
            return {};
        }
        return buffer.gpuAddress<T>();
    }

private:
    std::array<AllocatedBuffer<DeviceHostMappedBuffer>, FRAMES_IN_FLIGHT> buffers_;
};
